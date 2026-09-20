#include "entities.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.entity";

bool RegisterBacked::poll_(uint16_t *raw) {
  uint32_t stamped;
  if (!this->parent_->get_register(this->address_, raw, &stamped))
    return false;
  if (this->have_published_ && stamped == this->published_at_)
    return false;
  this->published_at_ = stamped;
  this->have_published_ = true;
  return true;
}

// --- number -----------------------------------------------------------------

void AeccNumber::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }

  if (this->param_ == ControlParam::NONE) {
    this->parent_->add_watch(this->address_, this->interval_);
    return;
  }

  if (this->parent_->control() == nullptr && this->param_ != ControlParam::RESTING_POWER) {
    ESP_LOGE(TAG, "controller parameter needs control: to be configured");
    this->mark_failed();
    return;
  }

  float value = std::isnan(this->initial_) ? this->current_() : this->initial_;
  if (this->restore_) {
    this->pref_ = this->make_entity_preference<float>();
    float restored;
    if (this->pref_.load(&restored))
      value = restored;
  }
  // A value stored while the bounds were wider must not survive tightening them.
  value = clamp(value, this->traits.get_min_value(), this->traits.get_max_value());
  this->apply_(value);
  this->publish_state(value);
}

void AeccNumber::apply_(float value) {
  auto *control = this->parent_->control();
  switch (this->param_) {
    case ControlParam::GRID_TARGET:
      control->set_grid_target((int32_t) value);
      break;
    case ControlParam::MAX_DISCHARGE:
      control->set_max_discharge((int32_t) value);
      break;
    case ControlParam::MAX_CHARGE:
      control->set_max_charge((int32_t) value);
      break;
    case ControlParam::MIN_SOC:
      control->set_min_soc((uint8_t) value);
      break;
    case ControlParam::MAX_SOC:
      control->set_max_soc((uint8_t) value);
      break;
    case ControlParam::RESTING_POWER:
      this->parent_->set_resting_power((int32_t) value);
      break;
    case ControlParam::MANUAL_SETPOINT:
      control->set_manual_setpoint((int32_t) value);
      break;
    case ControlParam::NONE:
      break;
  }
}

float AeccNumber::current_() {
  auto *control = this->parent_->control();
  switch (this->param_) {
    case ControlParam::GRID_TARGET:
      return (float) control->grid_target();
    case ControlParam::MAX_DISCHARGE:
      return (float) control->max_discharge();
    case ControlParam::MAX_CHARGE:
      return (float) control->max_charge();
    case ControlParam::MIN_SOC:
      return (float) control->min_soc();
    case ControlParam::MAX_SOC:
      return (float) control->max_soc();
    case ControlParam::RESTING_POWER:
      return (float) this->parent_->resting_power();
    case ControlParam::MANUAL_SETPOINT:
      return (float) control->manual_setpoint();
    default:
      return this->traits.get_min_value();
  }
}

void AeccNumber::control(float value) {
  if (this->param_ != ControlParam::NONE) {
    this->apply_(value);
    if (this->restore_)
      this->pref_.save(&value);
    this->publish_state(value);
    return;
  }
  const long raw = lroundf(value / this->scale_);
  const long lo = this->signed_ ? INT16_MIN : 0;
  const long hi = this->signed_ ? INT16_MAX : UINT16_MAX;
  if (raw < lo || raw > hi) {
    // Truncating would write an unrelated value to a real battery, which is worse than
    // refusing. Reachable whenever min_value or max_value is overridden past the register.
    ESP_LOGE(TAG, "%.2f is %ld at 0x%04X, outside a 16-bit register", value, raw, this->address_);
    return;
  }
  this->parent_->queue_write(this->address_, (uint16_t) raw);
  if (this->mirror_ != 0)
    this->parent_->queue_write(this->mirror_, (uint16_t) raw);
}

void AeccNumber::loop() {
  if (this->param_ != ControlParam::NONE)
    return;
  uint16_t raw;
  if (this->poll_(&raw))
    this->publish_state((this->signed_ ? (float) (int16_t) raw : (float) raw) * this->scale_);
}

void AeccNumber::dump_config() {
  LOG_NUMBER("", "AECC number", this);
  if (this->param_ == ControlParam::NONE) {
    ESP_LOGCONFIG(TAG, "  Register: 0x%04X, owned by the battery", this->address_);
  } else {
    ESP_LOGCONFIG(TAG, "  Controller parameter%s", this->restore_ ? ", restored" : "");
  }
}

// --- control mode -----------------------------------------------------------

void ControlModeSelect::setup() {
  if (this->parent_ == nullptr || this->parent_->control() == nullptr) {
    ESP_LOGE(TAG, "the mode select needs control: to be configured");
    this->mark_failed();
    return;
  }

  // The mode is this component's own state, so it has to survive a reboot; the option
  // list can change between builds, so a stored index would not.
  this->pref_ = this->make_entity_preference<ControlMode>();
  ControlMode restored = ControlMode::OFF;
  this->pref_.load(&restored);

  if (!this->apply_(restored)) {
    // Zero export is only offered with a meter, so a stored mode can stop existing.
    ESP_LOGW(TAG, "the stored mode is no longer available; falling back to off");
    this->apply_(ControlMode::OFF);
  }
  if (this->parent_->control()->mode() == ControlMode::OFF)
    ESP_LOGI(TAG, "control is off; nothing is commanded until a mode is selected");
}

bool ControlModeSelect::apply_(ControlMode mode) {
  for (const auto &option : this->modes_) {
    if (option.second != mode)
      continue;
    this->control(option.first);
    return true;
  }
  return false;
}

void ControlModeSelect::control(const std::string &value) {
  const auto found = this->modes_.find(value);
  if (found == this->modes_.end()) {
    ESP_LOGW(TAG, "'%s' is not a known mode", value.c_str());
    return;
  }
  this->parent_->control()->set_mode(found->second);
  this->pref_.save(&found->second);
  this->publish_state(value);
}

void ControlModeSelect::dump_config() { LOG_SELECT("", "AECC control mode", this); }

// --- switch -----------------------------------------------------------------

void AeccSwitch::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  this->parent_->add_watch(this->address_, this->interval_);
}

void AeccSwitch::write_state(bool state) {
  this->parent_->queue_write(this->address_, state ? 1 : 0);
}

void AeccSwitch::loop() {
  uint16_t raw;
  if (this->poll_(&raw))
    this->publish_state(raw != 0);
}

void AeccSwitch::dump_config() {
  LOG_SWITCH("", "AECC switch", this);
  ESP_LOGCONFIG(TAG, "  Register: 0x%04X", this->address_);
}

// --- select -----------------------------------------------------------------

void AeccSelect::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  this->parent_->add_watch(this->address_, this->interval_);
}

void AeccSelect::control(const std::string &value) {
  for (const auto &option : this->options_) {
    if (option.second != value)
      continue;
    this->parent_->queue_write(this->address_, option.first);
    return;
  }
  ESP_LOGW(TAG, "'%s' is not a known option for 0x%04X", value.c_str(), this->address_);
}

void AeccSelect::loop() {
  uint16_t raw;
  if (!this->poll_(&raw))
    return;
  const auto found = this->options_.find(raw);
  if (found == this->options_.end()) {
    // The value lists differ between firmware builds, so an unexpected value is a real
    // possibility rather than a fault.
    ESP_LOGW(TAG, "0x%04X reads %u, which is not in the option list", this->address_, raw);
    return;
  }
  this->publish_state(found->second);
}

void AeccSelect::dump_config() {
  LOG_SELECT("", "AECC select", this);
  ESP_LOGCONFIG(TAG, "  Register: 0x%04X", this->address_);
}

}  // namespace aecc
}  // namespace esphome
