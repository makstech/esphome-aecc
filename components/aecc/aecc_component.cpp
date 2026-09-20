#include "aecc_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <utility>

#ifdef USE_ESP32
#include <esp_task_wdt.h>
#endif

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc";
static const char *VERSION = "0.1.0";

static const size_t MAX_PENDING_WRITES = 16;

void AeccComponent::setup() {
#ifdef USE_ESP32
  this->mutex_ = xSemaphoreCreateMutex();
  if (this->mutex_ == nullptr) {
    this->mark_failed();
    return;
  }
  // The bus runs off the ESPHome loop so that an OTA, a WiFi reconnect or a slow
  // component cannot stall a control loop whose job is preventing export.
  if (this->control_ != nullptr) {
    // The control law clamps by SOC, so it needs one even when no sensor asked for it.
    this->add_watch(reg::SOC, 10000);
    if (this->control_->meter() != nullptr)
      this->control_->meter()->setup();
  }
#ifdef AECC_OTA_AWARE
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  xTaskCreate(&AeccComponent::bus_task_trampoline_, "aecc_bus", 8192, this, 2, &this->task_);
  if (this->task_ == nullptr)
    this->mark_failed();
#else
  ESP_LOGE(TAG, "this component needs an ESP32");
  this->mark_failed();
#endif
}

void AeccComponent::lock_() {
#ifdef USE_ESP32
  xSemaphoreTake(this->mutex_, portMAX_DELAY);
#endif
}

void AeccComponent::unlock_() {
#ifdef USE_ESP32
  xSemaphoreGive(this->mutex_);
#endif
}

void AeccComponent::add_watch(uint16_t address, uint32_t interval_ms) {
  // Entities register after the bus task is already iterating this vector.
  this->lock_();
  for (auto &w : this->watches_) {
    if (w.address == address) {
      w.interval_ms = std::min(w.interval_ms, interval_ms);
      this->unlock_();
      return;
    }
  }
  this->watches_.push_back({address, interval_ms, 0, 0, 0, false, 0, false});
  this->unlock_();
}

bool AeccComponent::get_register(uint16_t address, uint16_t *out, uint32_t *updated_at) {
  bool found = false;
  this->lock_();
  for (const auto &w : this->watches_) {
    if (w.address != address || !w.valid)
      continue;
    *out = w.raw;
    if (updated_at != nullptr)
      *updated_at = w.updated_at;
    found = true;
    break;
  }
  this->unlock_();
  return found;
}

bool AeccComponent::queue_write(uint16_t address, uint16_t value) {
  bool ok = false;
  this->lock_();
  if (this->writes_.size() < MAX_PENDING_WRITES) {
    this->writes_.push_back({address, value});
    ok = true;
  }
  this->unlock_();
  if (!ok)
    ESP_LOGW(TAG, "write queue full, dropped 0x%04X = %u", address, value);
  return ok;
}

void AeccComponent::request_backup() {
  this->lock_();
  if (!this->backup_running_) {
    this->backup_running_ = true;
    this->backup_island_ = 0;
    this->backup_addr_ = reg::CONFIG_ISLANDS[0].lo;
    this->backup_count_ = 0;
    this->backup_buf_.clear();
    this->backup_buf_.reserve(8192);
    ESP_LOGI(TAG, "backup started");
  }
  this->unlock_();
}

bool AeccComponent::backup_running() {
  this->lock_();
  const bool running = this->backup_running_;
  this->unlock_();
  return running;
}

std::string AeccComponent::backup_text() {
  this->lock_();
  std::string out = this->backup_done_;
  this->unlock_();
  return out;
}

void AeccComponent::backup_step_() {
  this->lock_();
  const size_t island = this->backup_island_;
  const uint16_t addr = this->backup_addr_;
  this->unlock_();

  const size_t islands = sizeof(reg::CONFIG_ISLANDS) / sizeof(reg::CONFIG_ISLANDS[0]);
  if (island >= islands) {
    if (this->dl_.configured() && !this->backup_ems_) {
      this->backup_ems_ = true;
      std::vector<uint16_t> ems;
      for (uint16_t a = 3000; a <= 3040; a++)
        ems.push_back(a);
      std::map<uint16_t, std::string> got;
      if (this->dl_.read(ems, got)) {
        this->lock_();
        for (const auto &kv : got)
          this->backup_buf_ += "ems " + std::to_string(kv.first) + " " + kv.second + "\n";
        this->unlock_();
      }
      return;
    }
    this->backup_ems_ = false;
    this->lock_();
    this->backup_done_.swap(this->backup_buf_);
    this->backup_buf_.clear();
    this->backup_running_ = false;
    const uint16_t n = this->backup_count_;
    this->unlock_();
    ESP_LOGI(TAG, "backup complete, %u registers", n);
    return;
  }

  uint16_t raw;
  char line[40];
  if (this->inverter_.read_one(addr, &raw, 2)) {
    snprintf(line, sizeof(line), "0x%04x %u\n", addr, raw);
    this->lock_();
    this->backup_buf_ += line;
    this->backup_count_++;
    this->unlock_();
  }

  this->lock_();
  this->backup_addr_++;
  if (this->backup_addr_ >= reg::CONFIG_ISLANDS[island].hi) {
    this->backup_island_++;
    if (this->backup_island_ < islands)
      this->backup_addr_ = reg::CONFIG_ISLANDS[this->backup_island_].lo;
  }
  this->unlock_();
}

void AeccComponent::request_restore(const std::string &text) {
  this->lock_();
  if (!this->restore_running_) {
    this->restore_text_ = text;
    this->restore_pos_ = 0;
    this->restore_written_ = this->restore_skipped_ = this->restore_failed_ = 0;
    this->restore_ems_.clear();
    this->restore_report_.clear();
    this->restore_running_ = true;
    ESP_LOGI(TAG, "restore started, %u bytes", (unsigned) text.size());
  }
  this->unlock_();
}

bool AeccComponent::restore_running() {
  this->lock_();
  const bool running = this->restore_running_;
  this->unlock_();
  return running;
}

std::string AeccComponent::restore_report() {
  this->lock_();
  std::string out = this->restore_report_;
  this->unlock_();
  return out;
}

void AeccComponent::restore_step_() {
  this->lock_();
  const size_t pos = this->restore_pos_;
  const size_t end = this->restore_text_.find('\n', pos);
  std::string line = this->restore_text_.substr(pos, end == std::string::npos ? std::string::npos
                                                                              : end - pos);
  const bool last = end == std::string::npos;
  this->restore_pos_ = last ? this->restore_text_.size() : end + 1;
  this->unlock_();

  // Values are not always numbers: the schedule slot is a CSV string and the period
  // timestamp contains spaces, so everything after the key is the value.
  auto split = [](const std::string &in, std::string &key, std::string &value) {
    const size_t a = in.find_first_not_of(" \t");
    if (a == std::string::npos)
      return false;
    const size_t b = in.find_first_of(" \t", a);
    if (b == std::string::npos)
      return false;
    const size_t c = in.find_first_not_of(" \t", b);
    if (c == std::string::npos)
      return false;
    key = in.substr(a, b - a);
    value = in.substr(c);
    while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
      value.pop_back();
    return true;
  };

  std::string key, value;
  if (split(line, key, value)) {
    if (key == "ems") {
      std::string ems_addr, ems_value;
      if (split(value, ems_addr, ems_value)) {
        this->lock_();
        this->restore_ems_[(uint16_t) strtoul(ems_addr.c_str(), nullptr, 10)] = ems_value;
        this->unlock_();
      }
    } else if (key.compare(0, 2, "0x") == 0) {
      const uint16_t addr = (uint16_t) strtoul(key.c_str() + 2, nullptr, 16);
      const uint16_t want = (uint16_t) strtoul(value.c_str(), nullptr, 10);
      if (addr < 0xA028 || addr >= 0xA0FA) {
        this->lock_();
        this->restore_skipped_++;
        this->unlock_();
      } else {
        uint16_t current;
        bool refused = false;
        const bool have = this->inverter_.read_one(addr, &current, 2, &refused);
        if (have && current == want) {
          this->lock_();
          this->restore_skipped_++;
          this->unlock_();
        } else if (!refused && this->inverter_.write_one(addr, want)) {
          this->lock_();
          this->restore_written_++;
          this->restore_refusals_ = 0;
          this->unlock_();
        } else {
          ESP_LOGW(TAG, "restore: 0x%04x = %u refused", addr, want);
          this->lock_();
          this->restore_failed_++;
          // Sustained illegal-address frames can stop this firmware responding. If the
          // unit is refusing everything, stop rather than send hundreds more.
          if (++this->restore_refusals_ >= RESTORE_REFUSALS_BEFORE_ABORT) {
            ESP_LOGE(TAG, "restore aborted: %u consecutive refusals, the unit is not "
                          "accepting writes",
                     this->restore_refusals_);
            this->restore_pos_ = this->restore_text_.size();
          }
          this->unlock_();
        }
      }
    }
  }

  if (!last)
    return;

  this->lock_();
  auto ems = this->restore_ems_;
  this->unlock_();
  size_t ems_written = 0;
  if (!ems.empty() && this->dl_.configured() && this->dl_.write(ems))
    ems_written = ems.size();

  char report[160];
  this->lock_();
  snprintf(report, sizeof(report),
           "written %u, already correct %u, refused %u, outside the settings island (not "
           "written) counted as skipped, ems %u\n",
           this->restore_written_, this->restore_skipped_, this->restore_failed_,
           (unsigned) ems_written);
  this->restore_report_ = report;
  this->restore_running_ = false;
  this->restore_text_.clear();
  this->unlock_();
  ESP_LOGI(TAG, "restore complete: %s", report);
}

#ifdef AECC_OTA_AWARE
void AeccComponent::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                        ota::OTAComponent *component) {
  switch (state) {
    case ota::OTA_STARTED:
    case ota::OTA_IN_PROGRESS:
      this->ota_active_ = true;
      break;
    case ota::OTA_ERROR:
    case ota::OTA_ABORT:
      this->ota_active_ = false;  // the update failed; carry on controlling
      break;
    case ota::OTA_COMPLETED:
      break;  // a reboot follows, so stay parked
  }
}
#endif

void AeccComponent::check_ports_() {
  auto *meter = this->control_ == nullptr ? nullptr : this->control_->meter();

  float volts;
  if (this->inverter_.read_f32(meter_reg::VOLTAGE, &volts, 2) && volts > 150.0f && volts < 300.0f) {
    ESP_LOGE(TAG, "the inverter bus answered %.1f V at the meter's voltage register: the two "
                  "RS485 leads look swapped. Not commanding anything.",
             volts);
    this->ports_ok_ = false;
    return;
  }

  // Only trust the inverter-shaped reply from the meter bus if it did not first identify
  // itself as a meter: the meter ignores addresses outside its own map rather than
  // raising an exception, so a stray read there can succeed by luck.
  if (meter != nullptr && !meter->confirms_meter() && meter->looks_like_inverter()) {
    ESP_LOGE(TAG, "the meter bus answered the inverter's SOC register: the two RS485 leads "
                  "look swapped. Not commanding anything.");
    this->ports_ok_ = false;
  }
}

void AeccComponent::reconcile_() {
  // 0xFE16 only modulates a command the energy manager is already running, so these are
  // what make the control loop work at all. The vendor app's AI mode rewrites them
  // underneath a running controller, which is why this repeats rather than runs once.
  const std::vector<uint16_t> want = {3000, 3003, 3020, 3021, 3022, 3026, 3029, 3030};
  std::map<uint16_t, std::string> got;
  if (!this->dl_.read(want, got)) {
    this->ems_ready_ = false;
    return;
  }

  uint8_t min_soc = 15, max_soc = 90;
  if (this->control_ != nullptr) {
    min_soc = this->control_->min_soc();
    max_soc = this->control_->max_soc();
  }
  // The slot is deliberately NOT tracked to the commanded setpoint. The datalogger
  // re-asserts it every 2-3 s and the loop overrides that at its own rate; keeping it
  // fixed and negative is what guarantees the resting state if the loop ever stops.
  const std::string slot = Datalogger::slot(this->resting_w_, max_soc, min_soc);

  std::map<uint16_t, std::string> fix;
  // 3026 positive exports unconditionally, regardless of house load, so it is not enough
  // to set it once: the app's AI mode writes it. It belongs in the reconcile set at a
  // site where export is prohibited.
  const std::pair<uint16_t, const char *> required[] = {
      {3000, "1"}, {3020, "6"}, {3021, "0"}, {3022, "0"},
      {3026, "0"}, {3029, "0"}, {3030, "1"}};
  for (const auto &r : required) {
    if (got[r.first] != r.second)
      fix[r.first] = r.second;
  }
  if (got[3003] != slot)
    fix[3003] = slot;

  if (fix.empty()) {
    if (!this->ems_ready_)
      ESP_LOGI(TAG, "EMS is configured for local control; slot reads '%s'", got[3003].c_str());
    this->ems_ready_ = true;
    return;
  }

  std::string names;
  for (const auto &kv : fix)
    names += " " + std::to_string(kv.first);
  ESP_LOGW(TAG, "EMS drifted, reasserting:%s (slot reads '%s', want '%s')", names.c_str(),
           got[3003].c_str(), slot.c_str());
  this->ems_ready_ = this->dl_.write(fix);
}

void AeccComponent::bus_task_() {
#ifdef USE_ESP32
  // ESPHome only subscribes its own loop task, so a wedged bus task would otherwise
  // leave the main loop healthy and the battery running whatever it last received.
  esp_task_wdt_add(nullptr);
#endif
  this->check_ports_();
  if (this->dl_.configured()) {
    // Until this has run the schedule slot is whatever the app last left, which may be a
    // positive (discharging) value. Controlling before then means a crash in the first
    // minute could leave the unit exporting.
    this->reconciled_at_ = millis();
    this->reconcile_();
  }

  for (;;) {
#ifdef USE_ESP32
    esp_task_wdt_reset();
#endif
    // The control loop is real time; polling and writes fill the gaps between ticks.
    if (this->control_ != nullptr && this->ports_ok_ &&
        (!this->dl_.configured() || this->ems_ready_)) {
      const uint32_t now = millis();
      if ((int32_t) (now - this->next_tick_) >= 0 &&
          this->ticks_since_housekeeping_ < TICKS_BEFORE_HOUSEKEEPING) {
        this->ticks_since_housekeeping_++;
        // Anchor to the deadline, not to now: an overrunning tick would otherwise fire
        // again immediately and starve the write queue and the poll sweep for ever.
        this->next_tick_ += this->control_->period_ms();
        if ((int32_t) (millis() - this->next_tick_) >= 0)
          this->next_tick_ = millis() + this->control_->period_ms();
        uint16_t soc = 0;
        const bool soc_valid = this->get_register(reg::SOC, &soc);
        this->control_->tick(&this->inverter_, soc, soc_valid);
        continue;
      }
    }

#ifdef AECC_OTA_AWARE
    if (this->ota_active_) {
      if (!this->ota_parked_) {
        this->ota_parked_ = true;
        // Park at zero and hold it there for the duration: a battery doing nothing cannot
        // export, and the reboot then hands over to the resting slot.
        this->inverter_.write_one(reg::SETPOINT, 0, 1);
        if (this->control_ != nullptr)
          this->control_->set_enabled(false);
        ESP_LOGW(TAG, "OTA in progress: setpoint parked, bus idle");
#ifdef USE_ESP32
        // Flash writes can starve this task for longer than the watchdog allows, and a
        // panic mid-update is a worse outcome than an unwatched idle task.
        esp_task_wdt_delete(nullptr);
#endif
      }
      delay(200);
      continue;
    }
    if (this->ota_parked_) {
      this->ota_parked_ = false;
#ifdef USE_ESP32
      esp_task_wdt_add(nullptr);
#endif
      if (this->control_ != nullptr)
        this->control_->set_enabled(true);
      ESP_LOGI(TAG, "OTA ended without rebooting; resuming");
    }
#endif

    this->ticks_since_housekeeping_ = 0;

    if (this->backup_running_) {
      this->backup_step_();
      continue;
    }

    if (this->restore_running_ && this->ports_ok_) {
      this->restore_step_();
      continue;
    }

    if (this->dl_.configured() && millis() - this->reconciled_at_ > this->reconcile_ms_) {
      this->reconciled_at_ = millis();
      this->reconcile_();
      continue;
    }

    // Writes next: a queued setting change should not wait behind a poll sweep.
    PendingWrite pending{0, 0};
    bool have_write = false;
    this->lock_();
    if (!this->writes_.empty()) {
      pending = this->writes_.front();
      this->writes_.erase(this->writes_.begin());
      have_write = true;
    }
    this->unlock_();

    if (have_write && !this->ports_ok_) {
      ESP_LOGW(TAG, "dropping write to 0x%04X: the RS485 leads look swapped", pending.address);
      have_write = false;
    }
    if (have_write) {
      const bool ok = this->inverter_.write_one(pending.address, pending.value);
      ESP_LOGD(TAG, "write 0x%04X = %u: %s", pending.address, pending.value, ok ? "ok" : "FAILED");
      if (ok) {
        // Read back so an entity never reports a value the inverter did not take.
        uint16_t raw;
        if (this->inverter_.read_one(pending.address, &raw)) {
          this->lock_();
          for (auto &w : this->watches_) {
            if (w.address == pending.address) {
              w.raw = raw;
              w.valid = true;
              w.updated_at = millis();
              w.last_read = millis();
            }
          }
          this->unlock_();
        }
      }
      continue;
    }

    uint16_t due_address = 0;
    bool have_due = false;
    const uint32_t now = millis();
    this->lock_();
    for (auto &w : this->watches_) {
      if (w.retired || now - w.last_read < w.interval_ms)
        continue;
      w.last_read = now;
      due_address = w.address;
      have_due = true;
      break;
    }
    this->unlock_();

    if (!have_due) {
      delay(this->control_ != nullptr ? 2 : 10);
      continue;
    }

    uint16_t raw;
    bool refused = false;
    const bool ok = this->inverter_.read_one(due_address, &raw, 3, &refused);
    this->lock_();
    for (auto &w : this->watches_) {
      if (w.address != due_address)
        continue;
      if (ok) {
        w.raw = raw;
        w.valid = true;
        w.updated_at = millis();
        w.exceptions = 0;
      } else if (refused && ++w.exceptions >= EXCEPTIONS_BEFORE_RETIRING) {
        w.retired = true;
        ESP_LOGE(TAG, "0x%04X is not a valid address on this unit; no longer polling it",
                 due_address);
      }
      break;
    }
    this->unlock_();
  }
}

void AeccComponent::loop() {
  if (!this->logged_ready_ && !this->watches_.empty()) {
    uint16_t raw;
    if (this->get_register(reg::SOC, &raw)) {
      ESP_LOGI(TAG, "inverter responding, SOC %u%%", raw);
      this->logged_ready_ = true;
    }
  }
}

void AeccComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "AECC inverter:");
  ESP_LOGCONFIG(TAG, "  Version: %s", VERSION);
  ESP_LOGCONFIG(TAG, "  Watched registers: %u", (unsigned) this->watches_.size());
  if (!this->ports_ok_)
    ESP_LOGE(TAG, "  RS485 leads appear swapped - the control loop is disabled");
  if (this->control_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Control: %u ms, meter %s", (unsigned) this->control_->period_ms(),
                  this->control_->meter() != nullptr ? this->control_->meter()->kind() : "NONE");
  }
}

}  // namespace aecc
}  // namespace esphome
