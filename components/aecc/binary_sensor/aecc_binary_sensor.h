#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"

#include "../aecc_component.h"

namespace esphome {
namespace aecc {

enum class Health : uint8_t {
  CONTROL_EFFECTIVE = 0,
  METER_OK,
  EMS_READY,
};

class AeccBinarySensor : public binary_sensor::BinarySensor, public Component, public AeccDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_health(Health health) { this->health_ = health; }

 protected:
  Health health_{Health::CONTROL_EFFECTIVE};
};

}  // namespace aecc
}  // namespace esphome
