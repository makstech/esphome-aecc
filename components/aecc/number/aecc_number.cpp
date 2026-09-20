#include "aecc_number.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.number";

void AeccNumber::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }

  if (this->param_ == ControlParam::NONE) {
    this->parent_->add_watch(this->address_, this->interval_);
    return;
  }

  if (this->parent_->control() == nullptr) {
    ESP_LOGE(TAG, "controller parameter configured but zero_export is not enabled");
    this->mark_failed();
    return;
  }

  this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
  float restored;
  if (this->pref_.load(&restored)) {
    // A value saved while the bounds were wider must not survive tightening them.
    restored = clamp(restored, this->traits.get_min_value(), this->traits.get_max_value());
    this->apply_(restored);
    this->publish_state(restored);
  } else {
    this->publish_state(this->current_());
  }
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
    default:
      return this->traits.get_min_value();
  }
}

void AeccNumber::control(float value) {
  if (this->param_ != ControlParam::NONE) {
    if (this->parent_->control() == nullptr)
      return;
    this->apply_(value);
    this->pref_.save(&value);
    this->publish_state(value);
    return;
  }
  this->parent_->queue_write(this->address_, (uint16_t) lroundf(value / this->scale_));
}

void AeccNumber::loop() {
  if (this->param_ != ControlParam::NONE)
    return;

  uint16_t raw;
  uint32_t stamped;
  if (!this->parent_->get_register(this->address_, &raw, &stamped))
    return;

  if (this->have_published_ && stamped == this->published_at_)
    return;
  this->published_at_ = stamped;
  this->have_published_ = true;
  this->publish_state((float) raw * this->scale_);
}

void AeccNumber::dump_config() {
  LOG_NUMBER("", "AECC number", this);
  if (this->param_ == ControlParam::NONE) {
    ESP_LOGCONFIG(TAG, "  Register: 0x%04X (inverter-owned, not restored)", this->address_);
  } else {
    ESP_LOGCONFIG(TAG, "  Controller parameter, restored from flash");
  }
}

}  // namespace aecc
}  // namespace esphome
