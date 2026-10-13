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

namespace esphome::socialhome_display {

/// A decoded picture: ARGB8888 (B, G, R, A bytes), ready for LVGL.
struct Pixels {
  int width{0};
  int height{0};
  uint8_t *data{nullptr};  ///< PSRAM when available; owned
  size_t size{0};

  Pixels() = default;
  Pixels(const Pixels &) = delete;
  Pixels &operator=(const Pixels &) = delete;
  ~Pixels();
};

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

/// Decode a PNG into ARGB8888 at `size`×`size` (box-filtered, aspect kept,
/// centred), optionally masked to a circle. nullptr when it is no PNG.
std::shared_ptr<Pixels> decode_png(const uint8_t *data, size_t len, int size, bool round);

class HttpWorker {
 public:
  explicit HttpWorker(const char *name, size_t max_queue) : name_(name), max_queue_(max_queue) {}
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
