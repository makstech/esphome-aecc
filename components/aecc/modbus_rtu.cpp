#include "modbus_rtu.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.rtu";

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 1) ? (uint16_t) ((crc >> 1) ^ 0xA001) : (uint16_t) (crc >> 1);
  }
  return crc;
}

size_t ModbusRtu::txn_(const uint8_t *pdu, size_t pdu_len, uint8_t *rx, size_t rx_cap, size_t want) {
  delay(FRAME_GAP_MS);

  while (this->uart_->available()) {
    uint8_t junk;
    this->uart_->read_byte(&junk);
  }

  uint8_t frame[16];
  frame[0] = this->unit_;
  memcpy(frame + 1, pdu, pdu_len);
  const uint16_t crc = crc16(frame, pdu_len + 1);
  frame[pdu_len + 1] = (uint8_t) (crc & 0xFF);
  frame[pdu_len + 2] = (uint8_t) (crc >> 8);
  this->uart_->write_array(frame, pdu_len + 3);
  this->uart_->flush();

  size_t got = 0;
  uint32_t deadline = millis() + this->reply_window_ms_;
  while ((int32_t) (millis() - deadline) < 0 && got < rx_cap) {
    if (!this->uart_->available()) {
      delay(1);
      continue;
    }
    if (!this->uart_->read_byte(&rx[got]))
      break;
    got++;
    if (got >= want)
      break;
    deadline = millis() + INTER_BYTE_MS;
  }
  return got;
}

bool ModbusRtu::read(uint16_t addr, uint16_t count, uint16_t *out, uint8_t tries, bool *exception) {
  if (exception != nullptr)
    *exception = false;
  if (this->uart_ == nullptr || count == 0 || count > 32)
    return false;

  const uint8_t pdu[5] = {0x03, (uint8_t) (addr >> 8), (uint8_t) (addr & 0xFF), (uint8_t) (count >> 8),
                          (uint8_t) (count & 0xFF)};
  const size_t want = 5 + 2 * count;
  uint8_t rx[5 + 2 * 32];

  for (uint8_t attempt = 0; attempt < tries; attempt++) {
    const size_t got = this->txn_(pdu, sizeof(pdu), rx, sizeof(rx), want);
    if (got < 5)
      continue;
    const uint16_t want_crc = (uint16_t) rx[got - 2] | (uint16_t) (rx[got - 1] << 8);
    if (crc16(rx, got - 2) != want_crc)
      continue;
    // FC3 replies carry no address, so a late reply to a previous request is otherwise
    // indistinguishable from this one's.
    if (rx[0] != this->unit_ || (rx[1] & 0x7F) != 0x03)
      continue;
    if (rx[1] & 0x80) {
      ESP_LOGV(TAG, "0x%04X: exception %u", addr, rx[2]);
      if (exception != nullptr)
        *exception = true;
      return false;  // an exception is a definite answer, not a lost frame
    }
    if (got < want || rx[2] != 2 * count)
      continue;
    for (uint16_t i = 0; i < count; i++)
      out[i] = (uint16_t) (rx[3 + 2 * i] << 8) | rx[4 + 2 * i];
    return true;
  }
  this->failures_++;
  return false;
}

bool ModbusRtu::read_one(uint16_t addr, uint16_t *out, uint8_t tries, bool *exception) {
  return this->read(addr, 1, out, tries, exception);
}

bool ModbusRtu::read_f32(uint16_t addr, float *out, uint8_t tries) {
  uint16_t w[2];
  if (!this->read(addr, 2, w, tries))
    return false;
  const uint32_t raw = ((uint32_t) w[0] << 16) | w[1];
  memcpy(out, &raw, sizeof(float));
  return true;
}

bool ModbusRtu::write_one(uint16_t addr, uint16_t value, uint8_t tries) {
  if (this->uart_ == nullptr)
    return false;

  const uint8_t pdu[5] = {0x06, (uint8_t) (addr >> 8), (uint8_t) (addr & 0xFF), (uint8_t) (value >> 8),
                          (uint8_t) (value & 0xFF)};
  uint8_t rx[16];

  for (uint8_t attempt = 0; attempt < tries; attempt++) {
    const size_t got = this->txn_(pdu, sizeof(pdu), rx, sizeof(rx), 8);
    if (got < 5)
      continue;
    const uint16_t want_crc = (uint16_t) rx[got - 2] | (uint16_t) (rx[got - 1] << 8);
    if (crc16(rx, got - 2) != want_crc)
      continue;
    if (rx[0] != this->unit_ || (rx[1] & 0x7F) != 0x06)
      continue;
    if (rx[1] & 0x80) {
      ESP_LOGW(TAG, "write 0x%04X = %u refused: exception %u", addr, value, rx[2]);
      return false;
    }
    // A conforming ack echoes the request. Without checking it, a late reply to an
    // earlier timed-out read can be mistaken for the acknowledgement of this write.
    if (got != 8 || rx[2] != (uint8_t) (addr >> 8) || rx[3] != (uint8_t) (addr & 0xFF) ||
        rx[4] != (uint8_t) (value >> 8) || rx[5] != (uint8_t) (value & 0xFF))
      continue;
    return true;
  }
  this->failures_++;
  return false;
}

}  // namespace aecc
}  // namespace esphome
