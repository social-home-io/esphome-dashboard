// SPDX-License-Identifier: MPL-2.0
//
// HTTP off the main loop. A worker thread runs one request at a time and
// hands the answer back through a mailbox the component drains in loop(), so
// a slow server never stalls LVGL. ESP-IDF uses esp_http_client; the host
// build (SDL screenshots) uses cpp-httplib, bundled with ESPHome's
// http_request component.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "png.h"

namespace esphome::socialhome_display {

struct HttpRequest {
  uint32_t id{0};
  std::string method{"GET"};
  std::string url;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  /// Decode a PNG answer into `pixels` at this size (0 = keep JSON / text).
  int decode_size{0};
  /// Make the decoded picture round (avatars).
  bool round{false};
  /// Decode a PNG answer at its own size into RGB565 (the map), streamed
  /// straight from the network; bigger than this is refused.
  int stream_width{0};
  int stream_height{0};
  /// Colour transparent map pixels are blended onto (0xRRGGBB).
  uint32_t background{0xFFFFFF};
};

struct HttpResponse {
  uint32_t id{0};
  int status{0};  ///< 0 = no answer (network error / timeout)
  std::string etag;
  std::string retry_after;
  std::string body;
  std::string error;
  std::shared_ptr<Pixels> pixels;
};

class HttpWorker {
 public:
  /// `priority`: FreeRTOS priority of the worker thread (ESP32). A worker
  /// that decodes big pictures runs at the main loop's so it can't starve it.
  explicit HttpWorker(const char *name, size_t max_queue, int priority = 5)
      : name_(name), max_queue_(max_queue), priority_(priority) {}
  ~HttpWorker();

  /// Starts the thread on first use.
  bool submit(HttpRequest request);
  /// Next finished request, if any (main loop only).
  bool poll(HttpResponse &out);
  /// Drop queued (not yet started) requests whose id fails `keep`.
  void cancel_if(const std::function<bool(const HttpRequest &)> &drop);
  size_t pending();

  static void set_timeout_ms(uint32_t ms) { timeout_ms_ = ms; }

 protected:
  void run_();
  static HttpResponse perform_(const HttpRequest &request);

  const char *name_;
  size_t max_queue_;
  int priority_;
  std::mutex lock_;
  std::condition_variable wake_;
  std::deque<HttpRequest> queue_;
  std::deque<HttpResponse> done_;
  bool running_{false};
  bool busy_{false};
  bool stop_{false};
  std::thread thread_;
  static uint32_t timeout_ms_;
};

}  // namespace esphome::socialhome_display
