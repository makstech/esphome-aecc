#include "controller.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.control";

float Controller::filter_(float sample) {
  const uint32_t now = millis();
  this->samples_[this->sample_head_] = sample;
  this->stamps_[this->sample_head_] = now;
  this->sample_head_ = (this->sample_head_ + 1) % MAX_SAMPLES;
  if (this->sample_count_ < MAX_SAMPLES)
    this->sample_count_++;

  float within[MAX_SAMPLES];
  size_t n = 0;
  for (size_t i = 0; i < this->sample_count_; i++) {
    if (now - this->stamps_[i] <= this->window_ms_)
      within[n++] = this->samples_[i];
  }
  if (n == 0)
    return sample;
  std::sort(within, within + n);
  return within[n / 2];
}

void Controller::limits_(uint16_t soc, bool soc_valid, int32_t *lo, int32_t *hi) const {
  *lo = -this->max_charge_;
  *hi = this->max_discharge_;
  if (!soc_valid)
    return;
  if (soc <= this->min_soc_)
    *hi = 0;  // empty: may still charge, must not discharge
  if (soc >= this->max_soc_)
    *lo = 0;  // full: may still discharge, must not charge
}

int16_t Controller::clamp_(float target, uint16_t soc, bool soc_valid) const {
  int32_t lo, hi;
  this->limits_(soc, soc_valid, &lo, &hi);
  return (int16_t) std::max((float) lo, std::min((float) hi, target));
}

int16_t Controller::step_(float grid_w, uint16_t soc, bool soc_valid) {
  const float error = grid_w - (float) this->grid_target_;
  const float delta = error * (error < 0 ? this->ease_per_tick_ : this->ramp_per_tick_);
  return this->clamp_((float) this->command_ + delta, soc, soc_valid);
}

int16_t Controller::predict_step_(uint16_t soc, bool soc_valid) {
  // What the battery was delivering when the meter took the reading being acted on.
  const size_t back = (this->modelled_head_ + MAX_DELAY_TICKS - this->delay_ticks_) % MAX_DELAY_TICKS;
  const float error = this->filtered_ - (float) this->grid_target_;
  // Toward export the whole error at once; toward import a share, as a margin for a model
  // that is only approximately the inverter.
  return this->clamp_(this->modelled_[back] + error * (error < 0 ? 1.0f : this->gain_), soc, soc_valid);
}

void Controller::advance_model_() {
  const float now = this->modelled_[this->modelled_head_];
  this->modelled_head_ = (this->modelled_head_ + 1) % MAX_DELAY_TICKS;
  this->modelled_[this->modelled_head_] = now + ((float) this->command_ - now) * this->alpha_;
}

void Controller::tick(ModbusRtu *inverter, uint16_t soc, bool soc_valid, bool may_command) {
  this->loops_++;

  // Loaded once: the main loop can change it between two reads, and a mode that reads
  // OFF then MANUAL would fall through to the zero-export branch.
  const ControlMode mode = this->parked_ || !may_command ? ControlMode::OFF : this->mode_;
  if (mode != this->last_mode_) {
    this->last_mode_ = mode;
    this->reset_filter_();
    std::fill(this->modelled_, this->modelled_ + MAX_DELAY_TICKS, 0.0f);
    // Whatever the register holds from before is not a push.
    this->guard_agreed_ = false;
  }

  // Read in every mode, so the meter stays a grid reading while nothing is regulated.
  float watts;
  const bool fresh = this->read_meter_(&watts);

  if (mode == ControlMode::OFF) {
    this->idle_();
    return;
  }

  if (mode == ControlMode::MANUAL) {
    const int32_t asked = this->manual_w_;
    int32_t lo, hi;
    this->limits_(soc, soc_valid, &lo, &hi);
    int32_t target = asked;
    if (target < lo)
      target = lo;
    if (target > hi)
      target = hi;
    if (target != asked && !this->clamped_)
      ESP_LOGW(TAG, "manual setpoint %d W held at %d W by the limits", (int) asked, (int) target);
    this->clamped_ = target != asked;
    this->command_ = (int16_t) target;
    this->deliver_(inverter);
    return;
  }

  if (fresh && this->masked_()) {
    // Hold the command: the reading is the push's blip.
  } else if (fresh) {
    if (this->frozen_ || !this->warm())
      this->command_ = 0;
    else if (this->law_ == ControlLaw::PREDICTIVE)
      this->command_ = this->predict_step_(soc, soc_valid);
    else
      this->command_ = this->step_(this->filtered_, soc, soc_valid);
  } else {
    const uint32_t age = this->meter_ == nullptr ? UINT32_MAX : this->meter_->age_ms();
    if (age < this->stale_after_ms_)
      return;  // a dropped frame is not a reason to change the command
    if (this->command_ != 0)
      ESP_LOGW(TAG, "meter stale for %u ms, commanding 0", (unsigned) age);
    this->command_ = 0;
  }

  this->deliver_(inverter);
}

void Controller::reset_filter_() {
  // filter_() scans samples_[0, sample_count_), so the head has to go back too.
  this->sample_count_ = 0;
  this->sample_head_ = 0;
  // The staleness clock would otherwise count the whole gap.
  this->frozen_since_ = millis();
  this->frozen_ = false;
}

bool Controller::read_meter_(float *watts) {
  if (this->meter_ == nullptr || !this->meter_->poll(watts)) {
    this->meter_ok_ = false;
    return false;
  }
  const uint32_t now = millis();
  if (*watts != this->frozen_value_) {
    this->frozen_value_ = *watts;
    this->frozen_since_ = now;
    this->frozen_ = false;
  } else if (now - this->frozen_since_ > FROZEN_AFTER_MS && !this->frozen_) {
    this->frozen_ = true;
    ESP_LOGW(TAG, "meter has returned exactly %.1f W for %u s; treating it as dead",
             *watts, (unsigned) ((now - this->frozen_since_) / 1000));
  }
  this->last_grid_ = *watts;
  this->meter_ok_ = !this->frozen_;
  if (!this->masked_())
    this->filtered_ = this->filter_(*watts);
  return true;
}

void Controller::idle_() {
  // Go quiet rather than command zero. The inverter falls back to its resting slot a few
  // seconds after the writes stop; a continuous zero is a literal 0 W command and the
  // slot never takes over.
  this->command_ = 0;
  // Nothing is being regulated, so the loop cannot claim health; leaving it at its last
  // value reads as a working loop.
  this->effective_ = false;
}

void Controller::note_readback_(int16_t read) {
  const bool agrees = read == this->written_;
  if (!agrees && this->guard_agreed_) {
    this->pushed_ = true;
    this->pushed_at_ = millis();
  }
  this->guard_agreed_ = agrees;
}

bool Controller::guard(ModbusRtu *inverter) {
  if (this->parked_ || this->last_mode_ == ControlMode::OFF ||
      millis() - this->guarded_at_ < GUARD_EVERY_MS)
    return false;
  this->guarded_at_ = millis();
  uint16_t raw;
  if (!inverter->read_one(reg::SETPOINT, &raw, 1))
    return true;
  this->note_readback_((int16_t) raw);
  if ((int16_t) raw != this->command_ && inverter->write_one(reg::SETPOINT, (uint16_t) this->command_, 1))
    this->written_ = this->command_;
  return true;
}

void Controller::deliver_(ModbusRtu *inverter) {
  // Read before writing: a push this write covered up unseen would pass for load.
  uint16_t before;
  if (inverter->read_one(reg::SETPOINT, &before, 1))
    this->note_readback_((int16_t) before);
  const bool wrote = inverter->write_one(reg::SETPOINT, (uint16_t) this->command_, 1);
  if (wrote)
    this->written_ = this->command_;
  this->guarded_at_ = millis();
  // Advanced whichever law is running, so switching to the predictive one starts from a
  // model that already knows what was commanded.
  this->advance_model_();
  if (!wrote) {
    // Do not keep integrating against a command the inverter never received, or the
    // error accumulates to the cap and lands all at once when the bus comes back. Not
    // zero either: the guard would then write that as a real 0 W command.
    this->command_ = this->written_;
    return;
  }

  const uint32_t now = millis();
  if (now - this->readback_at_ < READBACK_EVERY_MS)
    return;
  this->readback_at_ = now;

  uint16_t raw;
  if (!inverter->read_one(reg::SETPOINT, &raw, 1))
    return;
  this->readback_ = (int16_t) raw;

  bool honoured = this->readback_ == this->command_;

  // The register holding our value is not proof it is being acted on: with the energy
  // manager disabled the write persists and nothing moves. Check the battery too.
  if (honoured && (this->command_ > INERT_COMMAND_W || this->command_ < -INERT_COMMAND_W)) {
    uint16_t batt_raw;
    if (inverter->read_one(reg::BATTERY_POWER, &batt_raw, 1)) {
      const int16_t batt = (int16_t) batt_raw;
      if (batt < INERT_BATTERY_W && batt > -INERT_BATTERY_W)
        honoured = false;
    }
  }

  if (honoured) {
    this->mismatches_ = 0;
    if (!this->effective_) {
      ESP_LOGI(TAG, "setpoint is being honoured again");
      this->effective_ = true;
    }
    return;
  }
  if (this->mismatches_ < MISMATCHES_BEFORE_ALARM)
    this->mismatches_++;
  if (this->mismatches_ >= MISMATCHES_BEFORE_ALARM && this->effective_) {
    this->effective_ = false;
    ESP_LOGW(TAG, "commanded %d W, register reads %d W and the battery is not moving: the "
                  "inverter is ignoring the setpoint. Check that the EMS is enabled, the "
                  "schedule slot is non-zero and AI mode is off.",
             this->command_, this->readback_);
  }
}

}  // namespace aecc
}  // namespace esphome
