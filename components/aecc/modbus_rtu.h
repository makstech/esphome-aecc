#pragma once

#include "esphome/components/uart/uart.h"

namespace esphome {
namespace aecc {

uint16_t crc16(const uint8_t *data, size_t len);

/// Modbus RTU master for the AECC inverter and for wired meters.
///
/// Only FC3 and FC6 exist on the inverter firmware; FC1, FC2 and FC4 get no reply at
/// any address. Blocking, so it must run off the ESPHome loop.
class ModbusRtu {
 public:
  /// Back to back, the inverter answers only every other request - a clean alternating
  /// 50% loss that fabricates convincing structure in a scan instead of looking like
  /// noise. Measured: 0 ms gap -> 4/8 replies; 10 ms and above -> 8/8.
  static const uint32_t FRAME_GAP_MS = 12;
  /// The inverter's measured worst case. A meter answers far faster, and waiting the
  /// inverter's window on every failed meter poll is what starves the loop.
  static const uint32_t REPLY_WINDOW_MS = 350;
  static const uint32_t INTER_BYTE_MS = 60;

  ModbusRtu() = default;
  ModbusRtu(uart::UARTComponent *uart, uint8_t unit) : uart_(uart), unit_(unit) {}

  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_unit(uint8_t unit) { this->unit_ = unit; }
  void set_reply_window(uint32_t ms) { this->reply_window_ms_ = ms; }
  bool ready() const { return this->uart_ != nullptr; }

  /// exception, when given, distinguishes a definite refusal from a lost frame.
  bool read(uint16_t addr, uint16_t count, uint16_t *out, uint8_t tries = 3,
            bool *exception = nullptr);
  bool read_one(uint16_t addr, uint16_t *out, uint8_t tries = 3, bool *exception = nullptr);
  bool write_one(uint16_t addr, uint16_t value, uint8_t tries = 2);

  /// IEEE-754 from two registers, big-endian word order. The bundled meter's format.
  bool read_f32(uint16_t addr, float *out, uint8_t tries = 3);

  uint32_t failures() const { return this->failures_; }
  void reset_stats() { this->failures_ = 0; }

 protected:
  /// Returns bytes received. A truncated reply parses as real values at shifted
  /// addresses, so callers must check length as well as CRC.
  size_t txn_(const uint8_t *pdu, size_t pdu_len, uint8_t *rx, size_t rx_cap, size_t want);

  uart::UARTComponent *uart_{nullptr};
  uint8_t unit_{1};
  uint32_t reply_window_ms_{REPLY_WINDOW_MS};
  uint32_t failures_{0};
};

}  // namespace aecc
}  // namespace esphome
