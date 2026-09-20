#include "aecc_binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.binary_sensor";

void AeccBinarySensor::setup() {
  if (this->health_ == Health::EMS_READY) {
    if (this->parent_ == nullptr || !this->parent_->datalogger_configured()) {
      ESP_LOGE(TAG, "ems_ready needs a datalogger to be configured");
      this->mark_failed();
    }
    return;
  }
  if (this->parent_ == nullptr || this->parent_->control() == nullptr) {
    ESP_LOGE(TAG, "needs zero_export to be configured");
    this->mark_failed();
  }
}

void AeccBinarySensor::loop() {
  if (this->health_ == Health::EMS_READY) {
    this->publish_state(this->parent_->ems_ready());
    return;
  }
  auto *c = this->parent_->control();
  switch (this->health_) {
    case Health::CONTROL_EFFECTIVE:
      this->publish_state(c->effective());
      break;
    case Health::METER_OK:
      this->publish_state(c->meter_ok());
      break;
    case Health::EMS_READY:
      this->publish_state(this->parent_->ems_ready());
      break;
  }
}

void AeccBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "AECC health", this); }

}  // namespace aecc
}  // namespace esphome
