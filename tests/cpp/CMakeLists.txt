# SPDX-License-Identifier: MPL-2.0
# Host tests for the socialhome_display component's platform-free code:
# the JSON parser (against the pinned Social Home fixtures), the layout /
# URL helpers, the mDNS answer filtering, the sealed connect claim
# (against Social Home's shared vectors), the panel's strings and the cached
# theme.
#
#   cmake -S tests/cpp -B build/tests && cmake --build build/tests && ctest --test-dir build/tests -V

cmake_minimum_required(VERSION 3.20)
project(socialhome_display_tests C CXX)

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
# The PNG decoder the component pulls in (cg.add_library("pngle", "1.1.0")).
FetchContent_Declare(
  pngle
  URL https://github.com/kikuchan/pngle/archive/refs/tags/v1.1.0.tar.gz
  URL_HASH SHA256=ebdd8254445b07d59ca8cceafe49d2dfd71f5f57f2440d351f3006e134a5d5b6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pngle)
if(NOT TARGET pngle)
  add_library(pngle STATIC ${pngle_SOURCE_DIR}/src/pngle.c ${pngle_SOURCE_DIR}/src/miniz.c)
  target_include_directories(pngle PUBLIC ${pngle_SOURCE_DIR}/src)
endif()

# The sealed connect claim's crypto: the Mbed TLS line ESP-IDF 5.5 ships
# (3.6.x LTS, Apache-2.0), built from the release tarball (it carries the
# framework submodule the tag archive lacks).
FetchContent_Declare(
  MbedTLS
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.6/mbedtls-3.6.6.tar.bz2
  URL_HASH SHA256=8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INSTALL_MBEDTLS_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(MbedTLS)

set(COMPONENT ${CMAKE_CURRENT_SOURCE_DIR}/../../components/socialhome_display)
set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/display-api/v1/fixtures)

add_executable(socialhome_display_tests
  test_main.cpp
  test_parser.cpp
  test_util.cpp
  test_png.cpp
  test_discovery.cpp
  test_claim_seal.cpp
  test_panel_strings.cpp
  test_theme_cache.cpp
  ${COMPONENT}/claim_seal.cpp
  ${COMPONENT}/discovery.cpp
  ${COMPONENT}/panel_strings.cpp
  ${COMPONENT}/parser.cpp
  ${COMPONENT}/png.cpp
  ${COMPONENT}/theme_cache.cpp
  ${COMPONENT}/util.cpp
)
target_include_directories(socialhome_display_tests PRIVATE ${COMPONENT})
target_link_libraries(socialhome_display_tests PRIVATE ArduinoJson pngle MbedTLS::mbedcrypto)
target_compile_options(socialhome_display_tests PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_compile_definitions(socialhome_display_tests PRIVATE FIXTURES_DIR="${FIXTURES}")

# Not a test: times the map picture's decode (`png_bench map.png`).
add_executable(png_bench png_bench.cpp ${COMPONENT}/png.cpp)
target_include_directories(png_bench PRIVATE ${COMPONENT})
target_link_libraries(png_bench PRIVATE pngle)

enable_testing()
add_test(NAME socialhome_display_tests COMMAND socialhome_display_tests)
