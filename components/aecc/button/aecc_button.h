#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"

#include "../aecc_component.h"

namespace esphome {
namespace aecc {

class AeccBackupButton : public button::Button, public Component, public AeccDevice {
 protected:
  void press_action() override { this->parent_->request_backup(); }
};

}  // namespace aecc
}  // namespace esphome
