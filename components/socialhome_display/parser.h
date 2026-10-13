// SPDX-License-Identifier: MPL-2.0
//
// JSON → model for the Social Home display API v1 (ArduinoJson 7).
// No ESPHome / LVGL includes: the host tests compile this file with g++.
#pragma once

#include <cstddef>
#include <cstdint>

#include "model.h"

namespace esphome::socialhome_display {

enum class ParseResult : uint8_t {
  OK,
  BAD_JSON,       ///< not JSON, or not an object
  WRONG_VERSION,  ///< api_version is not 1: this firmware needs an update
  BAD_SHAPE,      ///< valid JSON but not the answer we asked for
};

const char *parse_result_str(ParseResult result);

/// Any board: /home, /messages, /notes, /map, /person/{id}.
ParseResult parse_board(const char *data, size_t len, Board &out);
/// The 423 body of a locked panel.
ParseResult parse_lock(const char *data, size_t len, LockScreen &out);
ParseResult parse_theme(const char *data, size_t len, Theme &out);
/// 201 of POST /pair.
ParseResult parse_pair_start(const char *data, size_t len, PairStart &out);
/// 200 of GET /pair/{id}.
ParseResult parse_pair_status(const char *data, size_t len, PairStatus &out);
/// `error.code` of an error body ("NIGHT", "TAB_OFF", …), empty if none.
std::string parse_error_code(const char *data, size_t len);

/// "#2f6f5e" → colour; anything else → invalid (neutral look).
Color parse_color(const char *text);

}  // namespace esphome::socialhome_display
