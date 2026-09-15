cmake_minimum_required(VERSION 3.16)
project(firmware_host_tests C)

set(IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "ESP-IDF source directory")
if(NOT EXISTS "${IDF_PATH}/components/json/cJSON/cJSON.c")
  message(FATAL_ERROR "Set IDF_PATH to an ESP-IDF checkout (including its cJSON submodule)")
endif()

add_executable(settings_console_test
  settings_console_test.c
  "${IDF_PATH}/components/json/cJSON/cJSON.c"
  "${IDF_PATH}/components/console/split_argv.c"
)
target_include_directories(settings_console_test PRIVATE "${IDF_PATH}/components/json/cJSON")
set_target_properties(settings_console_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_definitions(settings_console_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(settings_console_test PRIVATE /UNDEBUG)
else()
  target_compile_options(settings_console_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(settings_console_test PRIVATE m)
endif()

enable_testing()
add_test(NAME settings_console COMMAND settings_console_test)
