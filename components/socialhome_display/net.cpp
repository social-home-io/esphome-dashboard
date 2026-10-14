// SPDX-License-Identifier: MPL-2.0
#include "net.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_pthread.h>
#endif

#ifdef USE_HOST
#define USE_HTTP_REQUEST_HOST_H
#define CPPHTTPLIB_NO_EXCEPTIONS
#include "esphome/components/http_request/httplib.h"
#endif

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display.net";

/// Larger answers are refused: a board is ~20 kB, a 192 px avatar ~40 kB.
static constexpr size_t MAX_BODY = 512 * 1024;
/// A streamed picture (the map PNG, ~0.3–1 MB) may be this large.
static constexpr size_t MAX_STREAM = 8 * 1024 * 1024;

uint32_t HttpWorker::timeout_ms_ = 10000;

// ---- transport -----------------------------------------------------------

#ifdef USE_ESP32
namespace {

struct IdfContext {
  HttpResponse *response;
};

esp_err_t idf_event(esp_http_client_event_t *evt) {
  if (evt->event_id != HTTP_EVENT_ON_HEADER || evt->user_data == nullptr)
    return ESP_OK;
  auto *ctx = static_cast<IdfContext *>(evt->user_data);
  if (strcasecmp(evt->header_key, "ETag") == 0)
    ctx->response->etag = evt->header_value;
  else if (strcasecmp(evt->header_key, "Retry-After") == 0)
    ctx->response->retry_after = evt->header_value;
  return ESP_OK;
}

}  // namespace

HttpResponse HttpWorker::perform_(const HttpRequest &request) {
  HttpResponse response;
  response.id = request.id;
  IdfContext ctx{&response};
  esp_http_client_config_t config = {};
  config.url = request.url.c_str();
  config.timeout_ms = static_cast<int>(timeout_ms_);
  config.event_handler = idf_event;
  config.user_data = &ctx;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
  config.disable_auto_redirect = true;
  config.keep_alive_enable = false;
  if (request.url.rfind("https://", 0) == 0)
    config.crt_bundle_attach = esp_crt_bundle_attach;
  config.method = request.method == "POST" ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    response.error = "init failed";
    return response;
  }
  for (const auto &header : request.headers)
    esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
  esp_err_t err = esp_http_client_open(client, static_cast<int>(request.body.size()));
  if (err == ESP_OK && !request.body.empty()) {
    int wrote = esp_http_client_write(client, request.body.data(), static_cast<int>(request.body.size()));
    if (wrote < 0)
      err = ESP_FAIL;
  }
  if (err == ESP_OK) {
    int64_t length = esp_http_client_fetch_headers(client);
    response.status = esp_http_client_get_status_code(client);
    const bool stream = request.stream_width > 0 && response.status == 200;
    if (stream) {
      // Straight into the decoder: the PNG itself is never held whole.
      PngStream png(request.stream_width, request.stream_height, request.background);
      char chunk[4096];
      size_t total = 0;
      while (true) {
        int got = esp_http_client_read(client, chunk, sizeof(chunk));
        if (got < 0) {
          response.error = "read failed";
          response.status = 0;
          break;
        }
        if (got == 0)
          break;
        total += static_cast<size_t>(got);
        if (total > MAX_STREAM || !png.feed(reinterpret_cast<const uint8_t *>(chunk), static_cast<size_t>(got))) {
          response.error = total > MAX_STREAM ? "answer too large" : png.error();
          break;
        }
      }
      if (response.status == 200) {
        response.pixels = png.finish();
        if (response.pixels == nullptr && response.error.empty())
          response.error = "incomplete picture";
      }
    } else if (length > static_cast<int64_t>(MAX_BODY)) {
      response.error = "answer too large";
      response.status = 0;
    } else {
      char chunk[1024];
      while (true) {
        int got = esp_http_client_read(client, chunk, sizeof(chunk));
        if (got < 0) {
          response.error = "read failed";
          response.status = 0;
          break;
        }
        if (got == 0)
          break;
        response.body.append(chunk, got);
        if (response.body.size() > MAX_BODY) {
          response.error = "answer too large";
          response.status = 0;
          break;
        }
      }
    }
  } else {
    response.error = esp_err_to_name(err);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return response;
}
#endif  // USE_ESP32

#ifdef USE_HOST
HttpResponse HttpWorker::perform_(const HttpRequest &request) {
  HttpResponse response;
  response.id = request.id;
  // scheme://host[:port]  +  path
  size_t sep = request.url.find("://");
  size_t path_at = request.url.find('/', sep == std::string::npos ? 0 : sep + 3);
  std::string origin = path_at == std::string::npos ? request.url : request.url.substr(0, path_at);
  std::string path = path_at == std::string::npos ? "/" : request.url.substr(path_at);
  httplib::Client client(origin);
  if (!client.is_valid()) {
    response.error = "bad URL";
    return response;
  }
  time_t secs = timeout_ms_ / 1000;
  client.set_connection_timeout(secs, 0);
  client.set_read_timeout(secs, 0);
  client.set_write_timeout(secs, 0);
  httplib::Headers headers;
  for (const auto &header : request.headers)
    headers.emplace(header.first, header.second);
  httplib::Result result = request.method == "POST"
                               ? client.Post(path, headers, request.body, "")
                               : client.Get(path, headers);
  if (!result) {
    response.error = httplib::to_string(result.error());
    return response;
  }
  response.status = result->status;
  response.etag = result->get_header_value("ETag");
  response.retry_after = result->get_header_value("Retry-After");
  if (result->body.size() > (request.stream_width > 0 ? MAX_STREAM : MAX_BODY)) {
    response.status = 0;
    response.error = "answer too large";
    return response;
  }
  response.body = std::move(result->body);
  return response;
}
#endif  // USE_HOST

// ---- worker ----------------------------------------------------------------

HttpWorker::~HttpWorker() {
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    this->stop_ = true;
  }
  this->wake_.notify_all();
  if (this->thread_.joinable())
    this->thread_.join();
}

bool HttpWorker::submit(HttpRequest request) {
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    if (this->queue_.size() >= this->max_queue_)
      return false;
    if (!this->running_) {
#ifdef USE_ESP32
      // esp_http_client + TLS need a roomy stack in internal RAM.
      esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
      cfg.stack_size = 12 * 1024;
      cfg.prio = this->priority_;
      cfg.thread_name = this->name_;
      esp_pthread_set_cfg(&cfg);
#endif
      this->thread_ = std::thread([this]() { this->run_(); });
      this->running_ = true;
    }
    this->queue_.push_back(std::move(request));
  }
  this->wake_.notify_one();
  return true;
}

bool HttpWorker::poll(HttpResponse &out) {
  std::lock_guard<std::mutex> guard(this->lock_);
  if (this->done_.empty())
    return false;
  out = std::move(this->done_.front());
  this->done_.pop_front();
  return true;
}

void HttpWorker::cancel_if(const std::function<bool(const HttpRequest &)> &drop) {
  std::lock_guard<std::mutex> guard(this->lock_);
  this->queue_.erase(std::remove_if(this->queue_.begin(), this->queue_.end(), drop), this->queue_.end());
}

size_t HttpWorker::pending() {
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->queue_.size() + (this->busy_ ? 1 : 0) + this->done_.size();
}

void HttpWorker::run_() {
  while (true) {
    HttpRequest request;
    {
      std::unique_lock<std::mutex> guard(this->lock_);
      this->wake_.wait(guard, [this]() { return this->stop_ || !this->queue_.empty(); });
      if (this->stop_)
        return;
      request = std::move(this->queue_.front());
      this->queue_.pop_front();
      this->busy_ = true;
    }
    HttpResponse response = perform_(request);
    if (request.stream_width > 0 && response.status == 200 && response.pixels == nullptr && !response.body.empty()) {
      // Host build: the body arrived whole.
      PngStream png(request.stream_width, request.stream_height, request.background);
      png.feed(reinterpret_cast<const uint8_t *>(response.body.data()), response.body.size());
      response.pixels = png.finish();
      if (response.pixels == nullptr)
        response.error = png.error().empty() ? "incomplete picture" : png.error();
      response.body.clear();
      response.body.shrink_to_fit();
    } else if (request.decode_size > 0 && response.status == 200) {
      response.pixels = decode_png(reinterpret_cast<const uint8_t *>(response.body.data()), response.body.size(),
                                   request.decode_size, request.round);
      response.body.clear();
      response.body.shrink_to_fit();
      if (response.pixels == nullptr)
        response.error = "not a PNG";
    }
    if (response.status == 0)
      ESP_LOGW(TAG, "%s %s failed: %s", request.method.c_str(), request.url.c_str(), response.error.c_str());
    {
      std::lock_guard<std::mutex> guard(this->lock_);
      this->busy_ = false;
      this->done_.push_back(std::move(response));
    }
  }
}

}  // namespace esphome::socialhome_display
