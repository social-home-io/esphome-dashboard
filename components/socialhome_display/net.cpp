// SPDX-License-Identifier: MPL-2.0
#include "net.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include <pngle.h>

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

uint32_t HttpWorker::timeout_ms_ = 10000;

// ---- memory ------------------------------------------------------------

static uint8_t *big_alloc(size_t size) {
#ifdef USE_ESP32
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p != nullptr)
    return static_cast<uint8_t *>(p);
#endif
  return static_cast<uint8_t *>(malloc(size));
}

Pixels::~Pixels() { free(this->data); }

// ---- PNG ---------------------------------------------------------------

namespace {

struct PngTarget {
  uint32_t width{0};
  uint32_t height{0};
  uint8_t *rgba{nullptr};  // straight RGBA, source size
};

void png_init(pngle_t *pngle, uint32_t w, uint32_t h) {
  auto *t = static_cast<PngTarget *>(pngle_get_user_data(pngle));
  if (w == 0 || h == 0 || w > 1024 || h > 1024)
    return;
  t->width = w;
  t->height = h;
  t->rgba = big_alloc(static_cast<size_t>(w) * h * 4);
  if (t->rgba != nullptr)
    memset(t->rgba, 0, static_cast<size_t>(w) * h * 4);
}

void png_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  auto *t = static_cast<PngTarget *>(pngle_get_user_data(pngle));
  if (t->rgba == nullptr)
    return;
  for (uint32_t yy = y; yy < y + h && yy < t->height; yy++)
    for (uint32_t xx = x; xx < x + w && xx < t->width; xx++)
      memcpy(t->rgba + (static_cast<size_t>(yy) * t->width + xx) * 4, rgba, 4);
}

}  // namespace

std::shared_ptr<Pixels> decode_png(const uint8_t *data, size_t len, int size, bool round) {
  if (len < 8 || memcmp(data, "\x89PNG", 4) != 0 || size <= 0)
    return nullptr;
  PngTarget target;
  pngle_t *pngle = pngle_new();
  if (pngle == nullptr)
    return nullptr;
  pngle_set_user_data(pngle, &target);
  pngle_set_init_callback(pngle, png_init);
  pngle_set_draw_callback(pngle, png_draw);
  int fed = pngle_feed(pngle, data, len);
  pngle_destroy(pngle);
  if (fed < 0 || target.rgba == nullptr) {
    free(target.rgba);
    return nullptr;
  }
  auto out = std::make_shared<Pixels>();
  out->width = size;
  out->height = size;
  out->size = static_cast<size_t>(size) * size * 4;
  out->data = big_alloc(out->size);
  if (out->data == nullptr) {
    free(target.rgba);
    return nullptr;
  }
  memset(out->data, 0, out->size);
  // Fit the source into size×size, keeping its aspect, centred.
  float scale = std::min(size / static_cast<float>(target.width), size / static_cast<float>(target.height));
  int dw = std::max(1, static_cast<int>(std::lround(target.width * scale)));
  int dh = std::max(1, static_cast<int>(std::lround(target.height * scale)));
  int ox = (size - dw) / 2;
  int oy = (size - dh) / 2;
  float centre = (size - 1) / 2.0f;
  float radius = size / 2.0f;
  for (int y = 0; y < dh; y++) {
    uint32_t sy0 = static_cast<uint32_t>(y * target.height / dh);
    uint32_t sy1 = std::max(sy0 + 1, static_cast<uint32_t>((y + 1) * target.height / dh));
    for (int x = 0; x < dw; x++) {
      uint32_t sx0 = static_cast<uint32_t>(x * target.width / dw);
      uint32_t sx1 = std::max(sx0 + 1, static_cast<uint32_t>((x + 1) * target.width / dw));
      // Box filter, alpha-weighted so transparent edges don't darken.
      uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
      for (uint32_t sy = sy0; sy < sy1 && sy < target.height; sy++) {
        const uint8_t *row = target.rgba + static_cast<size_t>(sy) * target.width * 4;
        for (uint32_t sx = sx0; sx < sx1 && sx < target.width; sx++) {
          const uint8_t *p = row + sx * 4;
          r += p[0] * p[3];
          g += p[1] * p[3];
          b += p[2] * p[3];
          a += p[3];
          n++;
        }
      }
      uint8_t *o = out->data + (static_cast<size_t>(oy + y) * size + ox + x) * 4;
      if (a > 0) {
        o[0] = static_cast<uint8_t>(b / a);
        o[1] = static_cast<uint8_t>(g / a);
        o[2] = static_cast<uint8_t>(r / a);
      }
      uint32_t alpha = n > 0 ? a / n : 0;
      if (round) {
        float dx = ox + x - centre;
        float dy = oy + y - centre;
        float edge = radius - std::sqrt(dx * dx + dy * dy);  // >0 inside
        if (edge < 1.0f)
          alpha = edge <= 0.0f ? 0 : static_cast<uint32_t>(alpha * edge);
      }
      o[3] = static_cast<uint8_t>(alpha);
    }
  }
  free(target.rgba);
  return out;
}

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
    if (length > static_cast<int64_t>(MAX_BODY)) {
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
  if (result->body.size() > MAX_BODY) {
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
      cfg.prio = 5;
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
    if (request.decode_size > 0 && response.status == 200) {
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
