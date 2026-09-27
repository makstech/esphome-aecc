#pragma once

#include "esphome/core/defines.h"
#ifdef USE_WEBSERVER

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

#include "aecc_component.h"

namespace esphome {
namespace aecc {

/// Serves the last recorded trace as CSV. Recording is started by its button, so a trace
/// lines up with whatever was done to the battery while it ran.
class TraceHandler : public AsyncWebHandler, public Component {
 public:
  TraceHandler(web_server_base::WebServerBase *base) : base_(base) {}

  void setup() override {
    this->base_->init();
    this->base_->add_handler(this);
  }
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_parent(AeccComponent *parent) { this->parent_ = parent; }
  void set_url(const std::string &url) { this->url_ = url; }

  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET)
      return false;
#ifdef USE_ESP32
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(url) == this->url_;
#else
    return request->url() == this->url_.c_str();
#endif
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    if (this->parent_->trace_running()) {
      request->send(202, "text/plain", "recording, reload when it is done");
      return;
    }
    const std::string csv = this->parent_->trace_csv();
    if (csv.empty()) {
      request->send(404, "text/plain", "no trace yet; press Record trace");
      return;
    }
    auto *response = request->beginResponse(200, "text/csv", csv);
    request->send(response);
  }

 protected:
  web_server_base::WebServerBase *base_;
  AeccComponent *parent_{nullptr};
  std::string url_{"/aecc/trace"};
};

}  // namespace aecc
}  // namespace esphome

#endif  // USE_WEBSERVER
