# SPDX-License-Identifier: MPL-2.0
# Host tests for the socialhome_display component's platform-free code:
# the JSON parser (against the pinned Social Home fixtures) and the layout /
# URL helpers.
#
#   cmake -S tests/cpp -B build/tests && cmake --build build/tests && ctest --test-dir build/tests -V

cmake_minimum_required(VERSION 3.20)
project(socialhome_display_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# The same ArduinoJson ESPHome 2026.9.1 bundles (components/json).
FetchContent_Declare(
  ArduinoJson
  URL https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v7.4.3.tar.gz
  URL_HASH SHA256=a2a8a277fc3cd0404499199bf1468682ea3688f354be0eaf6af8648366911257
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ArduinoJson)

set(COMPONENT ${CMAKE_CURRENT_SOURCE_DIR}/../../components/socialhome_display)
set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/display-api/v1/fixtures)

add_executable(socialhome_display_tests
  test_main.cpp
  test_parser.cpp
  test_util.cpp
  ${COMPONENT}/parser.cpp
  ${COMPONENT}/util.cpp
)
target_include_directories(socialhome_display_tests PRIVATE ${COMPONENT})
target_link_libraries(socialhome_display_tests PRIVATE ArduinoJson)
target_compile_options(socialhome_display_tests PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_compile_definitions(socialhome_display_tests PRIVATE FIXTURES_DIR="${FIXTURES}")

enable_testing()
add_test(NAME socialhome_display_tests COMMAND socialhome_display_tests)
