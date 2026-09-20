#pragma once

#include "esphome/components/uart/uart.h"

#include "modbus_rtu.h"
#include "registers.h"

namespace esphome {
namespace aecc {

/// A source of grid active power, positive = importing.
///
/// Readings carry their age because the control law has to distinguish a fresh sample
/// from a stale one. That is free on a wired bus and emphatically not free once a
/// networked meter is in the path.
class MeterSource {
 public:
  virtual ~MeterSource() = default;
  virtual void setup() {}

  /// Blocking; called from the bus task. True means *watts holds a fresh reading.
  virtual bool poll(float *watts) = 0;
  virtual const char *kind() const = 0;

  /// Both devices are slave 1 at 9600, so only the register map distinguishes them.
  /// Used once at start-up to catch two swapped RS485 leads.
  virtual bool confirms_meter() { return true; }
  virtual bool looks_like_inverter() { return false; }

  uint32_t age_ms() const { return this->last_ok_ == 0 ? UINT32_MAX : millis() - this->last_ok_; }
  bool ever_read() const { return this->last_ok_ != 0; }
  uint32_t failures() const { return this->failures_; }

 protected:
  void mark_ok() { this->last_ok_ = millis(); }
  void mark_fail() { this->failures_++; }

  uint32_t last_ok_{0};
  uint32_t failures_{0};
};

/// The bundled SMeter-RS071 on its own RS485 pair. The default, because it is wired:
/// nothing in the control path can be delayed by WiFi.
class Rs071Meter : public MeterSource {
 public:
  void set_uart(uart::UARTComponent *uart) { this->bus_.set_uart(uart); }
  void set_unit(uint8_t unit) { this->bus_.set_unit(unit); }
  void set_reply_window(uint32_t ms) { this->bus_.set_reply_window(ms); }
  void set_register(uint16_t address) { this->address_ = address; }

  bool poll(float *watts) override;
  const char *kind() const override { return "RS071"; }
  bool confirms_meter() override;
  bool looks_like_inverter() override;

 protected:
  ModbusRtu bus_;
  uint16_t address_{meter_reg::ACTIVE_POWER};
};

}  // namespace aecc
}  // namespace esphome
