#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/defines.h"
#if defined(USE_OTA) && defined(USE_OTA_STATE_LISTENER)
#define AECC_OTA_AWARE
#include "esphome/components/ota/ota_backend.h"
#endif

#include "datalogger.h"
#include "meter.h"
#include "modbus_rtu.h"
#include "registers.h"
#include "controller.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

namespace esphome {
namespace aecc {

struct WatchedRegister {
  uint16_t address;
  uint32_t interval_ms;
  uint32_t last_read;
  uint32_t updated_at;
  uint16_t raw;
  bool valid;
  uint8_t exceptions;
  bool retired;
};

struct PendingWrite {
  uint16_t address;
  uint16_t value;
};

class AeccComponent : public Component
#ifdef AECC_OTA_AWARE
    ,
                      public ota::OTAGlobalStateListener
#endif
{
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_uart(uart::UARTComponent *uart) { this->inverter_.set_uart(uart); }
  void set_unit(uint8_t unit) { this->inverter_.set_unit(unit); }

  /// Entities declare what they need; the bus task reads them round-robin.
  void add_watch(uint16_t address, uint32_t interval_ms);

  /// Latest cached value, with when it was read. How stale a reading is matters more
  /// than the value itself once a bus starts dropping frames.
  bool get_register(uint16_t address, uint16_t *out, uint32_t *updated_at = nullptr);

  /// Queued for the bus task; returns false only if the queue is full.
  bool queue_write(uint16_t address, uint16_t value);

  uint32_t bus_failures() const { return this->inverter_.failures(); }

  /// Walk every configuration register into a text buffer. Runs a register at a time in
  /// the gaps between control ticks, so a backup never stalls the loop.
  void request_backup();
  bool backup_running();
  /// Empty until a backup has completed.
  std::string backup_text();

  /// Apply a previously captured backup. Only the settings island is written: the rest of
  /// the map is incompletely identified, and an illegal write is a worse prospect than an
  /// illegal read, which alone can stop this firmware responding.
  void request_restore(const std::string &text);
  bool restore_running();
  std::string restore_report();

  /// Optional; without it the component is monitoring and configuration only.
  void set_control(Controller *control) { this->control_ = control; }
  Controller *control() const { return this->control_; }

  Datalogger *datalogger() { return &this->dl_; }
  void set_datalogger_host(const std::string &host) { this->dl_.set_host(host); }
  void set_datalogger_port(uint16_t port) { this->dl_.set_port(port); }
  void set_resting_power(int32_t watts) { this->resting_w_ = watts; }
  int32_t resting_power() const { return this->resting_w_; }
  void set_reconcile_interval(uint32_t ms) { this->reconcile_ms_ = ms; }
  /// True once the EMS has been seen holding the state the control loop needs.
  bool ems_ready() const { return this->ems_ready_; }
  bool datalogger_configured() const { return this->dl_.configured(); }
  bool ports_ok() const { return this->ports_ok_; }

#ifdef AECC_OTA_AWARE
  /// The bus task is not the ESPHome loop, so it keeps commanding right through an
  /// update unless something stops it.
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                           ota::OTAComponent *component) override;
#endif

 protected:
  void bus_task_();
#ifdef USE_ESP32
  static void bus_task_trampoline_(void *arg) { static_cast<AeccComponent *>(arg)->bus_task_(); }
  TaskHandle_t task_{nullptr};
  // Constructed here, not in setup(): entities outrank the hub and call add_watch()
  // first, and taking a null semaphore trips a FreeRTOS assert.
  SemaphoreHandle_t mutex_{xSemaphoreCreateMutex()};
#endif
  void lock_();
  void unlock_();
  /// False in Off, where the component writes nothing at all.
  bool commanding_() const;
  /// Only valid on the bus task, which is the subscriber.
  void feed_wdt_();

  ModbusRtu inverter_;
  Controller *control_{nullptr};
  bool was_commanding_{false};
  uint32_t next_tick_{0};
  /// When every tick overruns its period, nothing else in the task ever runs -
  /// including the OTA park and the EMS reconcile, which matter most exactly then.
  static const uint8_t TICKS_BEFORE_HOUSEKEEPING = 8;
  uint8_t ticks_since_housekeeping_{0};
  std::vector<WatchedRegister> watches_;
  std::vector<PendingWrite> writes_;
  bool logged_ready_{false};

  bool backup_running_{false};
  size_t backup_island_{0};
  uint16_t backup_addr_{0};
  uint16_t backup_count_{0};
  std::string backup_buf_;
  std::string backup_done_;

  void backup_step_();
  void restore_step_();

  bool restore_running_{false};
  size_t restore_pos_{0};
  uint16_t restore_written_{0};
  uint16_t restore_skipped_{0};
  uint16_t restore_failed_{0};
  uint8_t restore_refusals_{0};
  static const uint8_t RESTORE_REFUSALS_BEFORE_ABORT = 5;
  std::string restore_text_;
  std::string restore_report_;
  std::map<uint16_t, std::string> restore_ems_;
  void reconcile_();
  /// Two swapped leads would point setpoint writes at the meter. Positive evidence only:
  /// a unit whose configuration has been wiped answers nothing, and that must not stop
  /// the component being used to restore it.
  void check_ports_();

  bool ports_ok_{true};
  volatile bool ota_active_{false};
  bool ota_parked_{false};

  Datalogger dl_;
  /// Negative: a dead controller must be left charging, never discharging.
  int32_t resting_w_{-300};
  uint32_t reconcile_ms_{60000};
  uint32_t reconciled_at_{0};
  bool ems_ready_{false};
  bool backup_ems_{false};

  /// A mistyped address would otherwise be polled forever. Sustained illegal reads can
  /// stop this firmware responding altogether.
  static const uint8_t EXCEPTIONS_BEFORE_RETIRING = 3;
};

/// Mixin for entities bound to the hub.
class AeccDevice {
 public:
  void set_parent(AeccComponent *parent) { this->parent_ = parent; }

 protected:
  AeccComponent *parent_{nullptr};
};

}  // namespace aecc
}  // namespace esphome
