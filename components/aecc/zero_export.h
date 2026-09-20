#pragma once

#include "meter.h"
#include "modbus_rtu.h"

#include <cmath>
#include <cstdint>

namespace esphome {
namespace aecc {

/// Holds the metered phase at a target by commanding the inverter's power setpoint.
///
/// The setpoint is signed: positive discharges to cover import, negative charges to
/// absorb export. Correction is asymmetric because the directions are not equally
/// forgiving - lowering the setpoint reduces export and is done in full and at once,
/// raising it is the direction that can cause export and is ramped.
class ZeroExport {
 public:
  static const size_t MAX_SAMPLES = 32;
  /// One sample is its own median. Commanding on it means a single glitched
  /// reading at boot can ask for the full discharge cap.
  static const size_t WARMUP_SAMPLES = 3;
  /// A meter that keeps answering with a value that never changes looks perfectly
  /// fresh. A real reading moves by tens of watts sample to sample.
  static const uint32_t FROZEN_AFTER_MS = 10000;

  void set_meter(MeterSource *meter) { this->meter_ = meter; }
  MeterSource *meter() const { return this->meter_; }

  void set_rate(float hz) {
    this->period_ms_ = (uint32_t) (1000.0f / (hz > 0.1f ? hz : 0.1f));
    this->recompute_ramp_();
  }
  void set_filter_window(uint32_t ms) { this->window_ms_ = ms; }
  void set_ramp_up(float fraction) {
    this->ramp_up_ = fraction;
    this->recompute_ramp_();
  }
  void set_stale_after(uint32_t ms) { this->stale_after_ms_ = ms; }
  void set_min_soc(uint8_t pct) { this->min_soc_ = pct; }
  void set_max_soc(uint8_t pct) { this->max_soc_ = pct; }

  /// Negative asks the inverter to export that many watts. Whether that is legal is
  /// the installation's business, not this component's: bound it with the number's
  /// min_value where it must not happen.
  void set_grid_target(int32_t watts) { this->grid_target_ = watts; }
  void set_max_discharge(int32_t watts) { this->max_discharge_ = watts < 0 ? 0 : watts; }
  void set_max_charge(int32_t watts) { this->max_charge_ = watts < 0 ? 0 : watts; }
  void set_enabled(bool enabled);

  int32_t grid_target() const { return this->grid_target_; }
  int32_t max_discharge() const { return this->max_discharge_; }
  int32_t max_charge() const { return this->max_charge_; }
  bool enabled() const { return this->enabled_; }
  uint8_t min_soc() const { return this->min_soc_; }
  uint8_t max_soc() const { return this->max_soc_; }

  uint32_t period_ms() const { return this->period_ms_; }
  void tick(ModbusRtu *inverter, uint16_t soc, bool soc_valid);

  /// False when the setpoint register stops agreeing with what we command.
  ///
  /// The register only modulates a command the energy manager is already running, so
  /// with the schedule slot at zero every write is accepted and silently ignored: the
  /// loop looks alive and the battery does nothing. Nothing else detects that.
  bool effective() const { return this->effective_; }
  int16_t readback() const { return this->readback_; }

  float last_grid_w() const { return this->last_grid_; }
  float filtered_grid_w() const { return this->filtered_; }
  int16_t command() const { return this->command_; }
  bool meter_ok() const { return this->meter_ok_; }
  uint32_t loops() const { return this->loops_; }
  bool warm() const { return this->sample_count_ >= WARMUP_SAMPLES; }

 protected:
  int16_t step_(float grid_w, uint16_t soc, bool soc_valid);
  /// ramp_up is the fraction of the error applied per tick at the rate it was validated
  /// at. Applying it per tick regardless of rate would make a faster loop ramp harder in
  /// the one direction that can export, so it is rescaled to hold the per-second
  /// response constant.
  void recompute_ramp_() {
    const float ticks = (float) this->period_ms_ / (float) VALIDATED_PERIOD_MS;
    this->ramp_per_tick_ = 1.0f - powf(1.0f - this->ramp_up_, ticks);
  }
  /// Median, not mean: the meter swings tens of watts at steady state and a mean lets a
  /// single spike move the setpoint. The window is in seconds so that changing the loop
  /// rate does not silently change how much smoothing is applied.
  float filter_(float sample);

  MeterSource *meter_{nullptr};

  static const uint32_t VALIDATED_PERIOD_MS = 500;  // the 2 Hz the control law was proven at

  uint32_t period_ms_{VALIDATED_PERIOD_MS};
  uint32_t window_ms_{1500};
  uint32_t stale_after_ms_{5000};
  float ramp_up_{0.35f};
  float ramp_per_tick_{0.35f};
  uint8_t min_soc_{15};
  uint8_t max_soc_{90};

  int32_t grid_target_{60};
  int32_t max_discharge_{600};
  int32_t max_charge_{2400};
  bool enabled_{true};

  float samples_[MAX_SAMPLES]{};
  uint32_t stamps_[MAX_SAMPLES]{};
  size_t sample_count_{0};
  size_t sample_head_{0};

  float last_grid_{0};
  float filtered_{0};
  int16_t command_{0};
  bool meter_ok_{false};
  uint32_t loops_{0};

  static const uint32_t READBACK_EVERY_MS = 5000;
  /// The datalogger re-asserts its own value every 2-3 s, so a single disagreement is
  /// expected; only a run of them means the writes are not landing.
  static const uint8_t MISMATCHES_BEFORE_ALARM = 3;
  /// A commanded discharge or charge this size should move the battery measurably.
  static const int16_t INERT_COMMAND_W = 150;
  static const int16_t INERT_BATTERY_W = 40;
  uint32_t readback_at_{0};
  uint8_t mismatches_{0};
  int16_t readback_{0};
  bool effective_{true};
  float frozen_value_{0};
  uint32_t frozen_since_{0};
  bool frozen_{false};
};

}  // namespace aecc
}  // namespace esphome
