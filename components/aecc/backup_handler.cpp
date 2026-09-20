#include "backup_handler.h"
#ifdef USE_WEBSERVER

#include "esphome/core/log.h"

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.backup";

void BackupHandler::handleRequest(AsyncWebServerRequest *request) {
  const bool running = this->parent_->backup_running();
  const std::string text = this->parent_->backup_text();

  if (!running && text.empty()) {
    this->parent_->request_backup();
    request->send(202, "text/plain", "backup started, reload in a minute");
    return;
  }
  if (text.empty()) {
    request->send(202, "text/plain", "backup in progress, reload in a minute");
    return;
  }

  auto *response = request->beginResponse(200, "text/plain", text);
  request->send(response);
  if (!running)
    this->parent_->request_backup();  // refresh in the background for the next fetch
}

void RestoreHandler::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                                size_t index, size_t total) {
  if (index == 0)
    this->upload_.clear();
  this->upload_.append(reinterpret_cast<const char *>(data), len);
  if (this->upload_.size() >= total)
    ESP_LOGI(TAG, "received %u bytes to restore", (unsigned) this->upload_.size());
}

void RestoreHandler::handleRequest(AsyncWebServerRequest *request) {
  if (this->parent_->restore_running()) {
    request->send(409, "text/plain", "a restore is already running");
    return;
  }
  if (this->upload_.empty()) {
    const std::string report = this->parent_->restore_report();
    request->send(200, "text/plain", report.empty() ? "nothing uploaded" : report.c_str());
    return;
  }
  this->parent_->request_restore(this->upload_);
  this->upload_.clear();
  request->send(202, "text/plain", "restore started; POST again with no file for the report");
}

}  // namespace aecc
}  // namespace esphome

#endif  // USE_WEBSERVER
