#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"

#include "aecc_component.h"

#include <map>
#include <string>

namespace esphome {
namespace aecc {

enum class ControlParam : uint8_t {
  NONE = 0,
  GRID_TARGET,
  MAX_DISCHARGE,
  MAX_CHARGE,
  MIN_SOC,
  MAX_SOC,
  RESTING_POWER,
  MANUAL_SETPOINT,
};

/// Reads a cached register and republishes it on every sweep, so a write the inverter
/// refused corrects the entity rather than leaving Home Assistant's optimistic value
/// standing. Shared by the writable entity types.
class RegisterBacked : public AeccDevice {
 public:
  void set_address(uint16_t address) { this->address_ = address; }
  void set_poll_interval(uint32_t interval_ms) { this->interval_ = interval_ms; }

 protected:
  /// True when a fresh read has landed since the last call.
  bool poll_(uint16_t *raw);

  uint16_t address_{0};
  uint32_t interval_{30000};
  uint32_t published_at_{0};
  bool have_published_{false};
};

/// Either a controller parameter or a battery setting.
///
/// The battery owns its settings, so a register-backed number never restores: writing a
/// remembered value back would undo a change made in the vendor app. A controller
/// parameter exists nowhere else, so it must persist.
class AeccNumber : public number::Number, public Component, public RegisterBacked {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Ahead of the hub, so a restored controller parameter is in place before the bus task
  // builds the schedule slot from it.
  float get_setup_priority() const override { return setup_priority::DATA + 1.0f; }

  void set_control_param(ControlParam param) { this->param_ = param; }
  void set_scale(float scale) { this->scale_ = scale; }
  void set_signed(bool is_signed) { this->signed_ = is_signed; }
  /// A second register holding the same setting. Output tops out at the lowest copy, so
  /// writing one alone reads back correct and changes nothing.
  void set_mirror(uint16_t address) { this->mirror_ = address; }
  void set_initial_value(float value) { this->initial_ = value; }
  void set_restore_value(bool restore) { this->restore_ = restore; }

 protected:
  void control(float value) override;
  void apply_(float value);
  float current_();

  ControlParam param_{ControlParam::NONE};
  float scale_{1.0f};
  bool signed_{false};
  uint16_t mirror_{0};
  float initial_{NAN};
  bool restore_{true};
  ESPPreferenceObject pref_;
};

/// Chooses what the component commands, and remembers it across a reboot.
class ControlModeSelect : public select::Select, public Component, public AeccDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA + 1.0f; }

  void add_mode(ControlMode mode, const std::string &label) { this->modes_[label] = mode; }

 protected:
  void control(const std::string &value) override;
  /// False when the mode is not in the offered list.
  bool apply_(ControlMode mode);

  std::map<std::string, ControlMode> modes_;
  ESPPreferenceObject pref_;
};

class AeccSwitch : public switch_::Switch, public Component, public RegisterBacked {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA + 1.0f; }

 protected:
  void write_state(bool state) override;
};

class AeccSelect : public select::Select, public Component, public RegisterBacked {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA + 1.0f; }

  void add_option(uint16_t value, const std::string &label) { this->options_[value] = label; }

 protected:
  void control(const std::string &value) override;

  std::map<uint16_t, std::string> options_;
};

}  // namespace aecc
}  // namespace esphome
