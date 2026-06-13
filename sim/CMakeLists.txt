cmake_minimum_required(VERSION 3.20)

project(wifi_proxy_ui_sim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

set(LVGL_DIR "" CACHE PATH "Path to the LVGL source directory installed by PlatformIO")
if(NOT LVGL_DIR)
    message(FATAL_ERROR "LVGL_DIR is not set. Run through `make sim-build`.")
endif()
if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL source not found at ${LVGL_DIR}. Run `make build` first.")
endif()

add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")

target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE=1)

add_executable(wifi_proxy_ui_sim main.cpp)
target_include_directories(wifi_proxy_ui_sim PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(wifi_proxy_ui_sim PRIVATE LV_CONF_INCLUDE_SIMPLE=1)
target_link_libraries(wifi_proxy_ui_sim PRIVATE lvgl PkgConfig::SDL2)
