#pragma once

#include "meter.h"
#include "modbus_rtu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace esphome {
namespace aecc {

/// What the component is allowed to command. Off writes nothing at all, so the battery
/// answers to the vendor app, the cloud or anything else on the bus.
enum class ControlMode : uint8_t {
  OFF = 0,
  ZERO_EXPORT,
  MANUAL,
};

/// How zero export turns the grid error into a command.
enum class ControlLaw : uint8_t {
  /// Adds a share of the error to the last command every tick.
  CLASSIC = 0,
  /// Adds the error to what a model says the battery is delivering, so the part of the
  /// last command still on its way is not asked for twice.
  PREDICTIVE,
};

/// Drives the inverter's power setpoint, either to hold the metered phase at a target or
/// to a value someone set directly.
///
/// The setpoint is signed: positive discharges to cover import, negative charges to
/// absorb export. Zero-export correction is asymmetric because the directions are not
/// equally forgiving - lowering the setpoint reduces export and is done in full and at
/// once, raising it is the direction that can cause export and is ramped.
class Controller {
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
    this->recompute_timing_();
  }
  float rate() const { return 1000.0f / (float) this->period_ms_; }
  void set_filter_window(uint32_t ms) { this->window_ms_ = ms; }
  uint32_t filter_window() const { return this->window_ms_; }
  /// Import must persist this long before zero export covers it; export is corrected at once.
  void set_rise_delay(uint32_t ms) { this->rise_ms_ = ms; }
  uint32_t rise_delay() const { return this->rise_ms_; }
  void set_ramp_up(float fraction) {
    this->ramp_up_ = fraction;
    this->recompute_timing_();
  }
  void set_law(ControlLaw law) { this->law_ = law; }
  ControlLaw law() const { return this->law_; }
  void set_predictive_gain(float gain) { this->gain_ = gain; }
  float predictive_gain() const { return this->gain_; }
  void set_actuator_lag(uint32_t ms) {
    this->lag_ms_ = ms;
    this->recompute_timing_();
  }
  void set_meter_delay(uint32_t ms) {
    this->delay_ms_ = ms;
    this->recompute_timing_();
  }
  uint32_t meter_delay() const { return this->delay_ms_; }
  /// With the predictive law, PV that rose on the port since the meter's reading is
  /// absorbed before the meter shows it as export. Falls are left to the meter.
  void set_pv_feed_forward(bool on) { this->pv_feed_forward_ = on; }
  void set_stale_after(uint32_t ms) { this->stale_after_ms_ = ms; }
  void set_min_soc(uint8_t pct) { this->min_soc_ = pct; }
  void set_max_soc(uint8_t pct) { this->max_soc_ = pct; }
  /// A point of the charge taper, added in rising SOC order: the charge limit runs in a
  /// straight line between points and holds the last one above it.
  void add_charge_taper(uint8_t soc, int32_t watts) { this->taper_.push_back({soc, watts}); }

  /// Negative asks the inverter to export that many watts. Whether that is legal is
  /// the installation's business, not this component's: bound it with the number's
  /// min_value where it must not happen.
  void set_grid_target(int32_t watts) { this->grid_target_ = watts; }
  void set_max_discharge(int32_t watts) { this->max_discharge_ = watts < 0 ? 0 : watts; }
  void set_max_charge(int32_t watts) { this->max_charge_ = watts < 0 ? 0 : watts; }
  void set_mode(ControlMode mode) { this->mode_ = mode; }
  /// Manual target in watts, positive discharging, the same sign as the setpoint
  /// register and as EMHASS's p_sto_pos.
  void set_manual_setpoint(int32_t watts) { this->manual_w_ = watts; }
  // Held by the bus task across an OTA, so resuming cannot override the chosen mode.
  void set_parked(bool parked) { this->parked_ = parked; }

  int32_t grid_target() const { return this->grid_target_; }
  int32_t max_discharge() const { return this->max_discharge_; }
  int32_t max_charge() const { return this->max_charge_; }
  ControlMode mode() const { return this->mode_; }
  int32_t manual_setpoint() const { return this->manual_w_; }
  uint8_t min_soc() const { return this->min_soc_; }
  uint8_t max_soc() const { return this->max_soc_; }

  uint32_t period_ms() const { return this->period_ms_; }
  // may_command is false while the energy manager is unconfirmed: the meter is still read,
  // and nothing is written.
  void tick(ModbusRtu *inverter, uint16_t soc, bool soc_valid, bool may_command);
  /// Between ticks, checks the setpoint register and puts the command back when the
  /// datalogger's periodic push has overwritten it. The loop then ignores the meter for a
  /// moment, since the blip that follows is the push, not the house.
  bool guard(ModbusRtu *inverter);

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
  /// Whether this tick's meter and telemetry reads answered.
  bool meter_fresh() const { return this->meter_fresh_; }
  bool telemetry_fresh() const { return this->battery_fresh_; }
  int16_t battery_w() const { return this->battery_w_; }
  int16_t grid_port_w() const { return this->grid_port_w_; }
  int16_t backup_w() const { return this->backup_w_; }
  int16_t setpoint_w() const { return this->setpoint_read_; }
  /// Positive producing.
  int16_t pv_w() const { return this->pv_w_; }
  /// Datalogger pushes seen so far.
  uint32_t pushes() const { return this->pushes_; }
  uint32_t loops() const { return this->loops_; }
  bool warm() const { return this->sample_count_ >= WARMUP_SAMPLES; }

 protected:
  int16_t step_(float grid_w, uint16_t soc, bool soc_valid);
  /// The state of charge window, as the bounds the command may take.
  void limits_(uint16_t soc, bool soc_valid, int32_t *lo, int32_t *hi) const;
  /// The charge limit the taper sets at this SOC; INT32_MAX below its first point.
  int32_t taper_limit_(uint16_t soc) const;
  /// Commands nothing and forgets everything the loop was carrying.
  bool read_meter_(float *watts);
  void idle_();
  /// Starts the median filter again. Whatever it holds is from before the mode changed,
  /// and the stamps are outside the window, so one tick would act on a single sample.
  void reset_filter_();
  /// One read across the telemetry window from the battery to the PV port, at the start of
  /// every tick: separate frames would each pay the inter-frame gap.
  void read_telemetry_(ModbusRtu *inverter);
  /// How much the PV port rose since the meter took its reading.
  float pv_rise_() const;
  /// Writes command_ and, occasionally, checks the inverter is acting on it.
  void deliver_(ModbusRtu *inverter);
  /// Corrections are sized for the rate the law was validated at. The inverter takes about
  /// a second to respond, so until it does a faster loop sees the same error on every tick;
  /// scaling each tick's share by its length keeps the correction per second the same in
  /// both directions.
  void recompute_timing_() {
    const float share = (float) this->period_ms_ / (float) VALIDATED_PERIOD_MS;
    this->ramp_per_tick_ = std::min(1.0f, this->ramp_up_ * share);
    this->ease_per_tick_ = std::min(1.0f, share);
    this->alpha_ = 1.0f - expf(-(float) this->period_ms_ / (float) std::max<uint32_t>(this->lag_ms_, 1));
    this->delay_ticks_ = std::min<size_t>(MAX_DELAY_TICKS - 1,
                                          (this->delay_ms_ + this->period_ms_ / 2) / this->period_ms_);
  }
  int16_t clamp_(float target, uint16_t soc, bool soc_valid) const;
  int16_t predict_step_(float grid_w, uint16_t soc, bool soc_valid);
  /// When the battery stops following, the measured power replaces the model: no windup.
  void anchor_if_lost_();
  /// Moves the model of what the battery delivers one tick toward what was commanded.
  void advance_model_();
  /// Median, not mean: the meter swings tens of watts at steady state and a mean lets a
  /// single spike move the setpoint. The window is in seconds so that changing the loop
  /// rate does not silently change how much smoothing is applied.
  float filter_(float sample);

  MeterSource *meter_{nullptr};

  static const uint32_t VALIDATED_PERIOD_MS = 500;  // the 2 Hz the control law was proven at

  uint32_t period_ms_{VALIDATED_PERIOD_MS};
  uint32_t window_ms_{1500};
  uint32_t rise_ms_{2000};
  float rise_floor_{0};
  uint32_t stale_after_ms_{5000};
  float ramp_up_{0.35f};
  float ramp_per_tick_{0.35f};
  /// The share of an excess-discharge error removed per tick; the whole of it at 2 Hz.
  float ease_per_tick_{1.0f};

  ControlLaw law_{ControlLaw::CLASSIC};
  float gain_{0.7f};
  /// The inverter follows a new setpoint as a first-order lag of about this; the meter
  /// reports what it did about delay_ms_ later.
  uint32_t lag_ms_{400};
  uint32_t delay_ms_{500};
  static const size_t MAX_DELAY_TICKS = 32;
  float alpha_{0.71f};
  size_t delay_ticks_{1};
  /// Modelled battery power, one entry per tick, newest at modelled_head_.
  float modelled_[MAX_DELAY_TICKS]{};
  size_t modelled_head_{0};
  uint8_t min_soc_{15};
  uint8_t max_soc_{90};
  struct TaperPoint {
    uint8_t soc;
    int32_t watts;
  };
  std::vector<TaperPoint> taper_;

  int32_t grid_target_{60};
  int32_t max_discharge_{600};
  int32_t max_charge_{2400};
  // Off until someone selects a mode: a freshly flashed node should not start
  // commanding a grid-connected battery on its own.
  volatile ControlMode mode_{ControlMode::OFF};
  volatile int32_t manual_w_{0};
  bool parked_{false};
  ControlMode last_mode_{ControlMode::OFF};
  bool clamped_{false};

  float samples_[MAX_SAMPLES]{};
  uint32_t stamps_[MAX_SAMPLES]{};
  size_t sample_count_{0};
  size_t sample_head_{0};

  float last_grid_{0};
  float filtered_{0};
  bool meter_fresh_{false};
  int16_t command_{0};
  int16_t battery_w_{0};
  int16_t grid_port_w_{0};
  int16_t backup_w_{0};
  int16_t setpoint_read_{0};
  int16_t pv_w_{0};
  bool battery_fresh_{false};
  bool pv_feed_forward_{true};
  /// PV port power per tick, newest at pv_head_; NAN where the read failed.
  float pv_hist_[MAX_DELAY_TICKS]{};
  size_t pv_head_{0};
  uint32_t pushes_{0};
  static const int16_t FOLLOW_TOLERANCE_W = 150;
  static const uint8_t LOST_TICKS = 4;
  uint8_t lost_ticks_{0};
  bool meter_ok_{false};
  uint32_t loops_{0};

  static const uint32_t READBACK_EVERY_MS = 5000;
  static const uint32_t GUARD_EVERY_MS = 100;
  /// Measured: a push shows on the meter for about half a second after it lands.
  static const uint32_t PUSH_MASK_MS = 800;
  /// The inverter leaves a charge setpoint smaller than this at rest, so a small PV
  /// surplus would leak out; Zero export rounds such a charge up to it.
  static const int16_t MIN_CHARGE_W = 50;
  /// Below this the command is noise around zero and is left alone.
  static const int16_t CHARGE_DEADBAND_W = 15;
  /// A masked reading this far past what the pushes could add is the house's own export.
  static const int16_t PUSH_EXPORT_MARGIN_W = 50;
  /// How far the pushes in the current mask raised the setpoint over ours.
  int32_t push_excess_{0};
  uint32_t guarded_at_{0};
  uint32_t pushed_at_{0};
  bool pushed_{false};
  /// A push is a disagreement after an agreement. One that persists is the inverter not
  /// taking the setpoint at all, which must not hold the loop still.
  bool guard_agreed_{false};
  /// The value last written to the setpoint register; any other reading is someone else's.
  int16_t written_{0};
  void note_readback_(int16_t read);
  bool masked_() const { return this->pushed_ && millis() - this->pushed_at_ < PUSH_MASK_MS; }
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
