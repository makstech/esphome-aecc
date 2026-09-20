#pragma once

#include "esphome/core/defines.h"
#ifdef USE_WEBSERVER

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

#include "aecc_component.h"

namespace esphome {
namespace aecc {

/// Serves the configuration backup at a URL you can curl or click.
///
/// Reading every configuration register takes tens of seconds at 9600 baud, far longer
/// than an HTTP request should wait, so a fetch starts the walk and returns the last
/// completed backup. The second fetch gets the fresh one.
class BackupHandler : public AsyncWebHandler, public Component {
 public:
  BackupHandler(web_server_base::WebServerBase *base) : base_(base) {}

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

  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  web_server_base::WebServerBase *base_;
  AeccComponent *parent_{nullptr};
  std::string url_{"/aecc/backup"};
};

/// Accepts a backup file back. Multipart upload, so `curl -F file=@aecc-backup.txt` works
/// and so does a plain browser form.
class RestoreHandler : public AsyncWebHandler, public Component {
 public:
  RestoreHandler(web_server_base::WebServerBase *base) : base_(base) {}

  void setup() override {
    this->base_->init();
    this->base_->add_handler(this);
  }
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_parent(AeccComponent *parent) { this->parent_ = parent; }
  void set_url(const std::string &url) { this->url_ = url; }

  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_POST)
      return false;
#ifdef USE_ESP32
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(url) == this->url_;
#else
    return request->url() == this->url_.c_str();
#endif
  }

  bool isRequestHandlerTrivial() const override { return false; }

  /// Raw body, not multipart: the ESP-IDF server only dispatches multipart to
  /// handleUpload when the web_server OTA platform is compiled in.
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                  size_t total) override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  web_server_base::WebServerBase *base_;
  AeccComponent *parent_{nullptr};
  std::string url_{"/aecc/restore"};
  std::string upload_;
};

}  // namespace aecc
}  // namespace esphome

#endif  // USE_WEBSERVER
