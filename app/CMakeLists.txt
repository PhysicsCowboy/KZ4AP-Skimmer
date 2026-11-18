# The app's logic without Qt, and its tests: always built, so CI covers them on a machine without Qt.
add_library(kz4ap_app_core
  core/cpu_meter.cpp
  core/delivery_watch.cpp
  core/display_feed.cpp
  core/known_call_download.cpp
  core/track_table.cpp
  core/recording_name.cpp
  core/waterfall.cpp
)
target_include_directories(kz4ap_app_core PUBLIC core)
target_link_libraries(kz4ap_app_core PUBLIC kz4ap::engine PRIVATE nlohmann_json::nlohmann_json)
# The program's version, for the User-Agent of the known-call list's download (known_call_download.hpp).
target_compile_definitions(kz4ap_app_core PRIVATE KZ4AP_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(kz4ap_app_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_app_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap_app_tests
  tests/waterfall_test.cpp
  tests/track_table_test.cpp
  tests/display_feed_test.cpp
  tests/cpu_meter_test.cpp
  tests/delivery_watch_test.cpp
  tests/recording_name_test.cpp
  tests/known_call_download_test.cpp
)
target_link_libraries(kz4ap_app_tests PRIVATE kz4ap_app_core GTest::gtest_main)
gtest_discover_tests(kz4ap_app_tests)

# The desktop app itself, only where Qt 6 Widgets is found: point CMake at it with CMAKE_PREFIX_PATH, for example
#   cmake --preset windows -DCMAKE_PREFIX_PATH=<Qt>/6.8.3/msvc2022_64
# Without Qt everything else configures, builds and tests as usual.
find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(NOT Qt6Widgets_FOUND OR NOT Qt6Network_FOUND)
  message(STATUS "Qt 6 Widgets and Network not found: kz4ap-skimmer is not built")
  return()
endif()
message(STATUS "Qt ${Qt6Widgets_VERSION} found: building kz4ap-skimmer")

add_executable(kz4ap-skimmer
  src/main.cpp
  src/main_window.cpp
  src/main_window.hpp
  src/call_list_model.cpp
  src/call_list_model.hpp
  src/track_list_model.cpp
  src/track_list_model.hpp
  src/waterfall_widget.cpp
  src/waterfall_widget.hpp
)
set_target_properties(kz4ap-skimmer PROPERTIES AUTOMOC ON WIN32_EXECUTABLE ON)
target_link_libraries(kz4ap-skimmer PRIVATE kz4ap_app_core Qt6::Widgets Qt6::Network)
if(MSVC)
  target_compile_options(kz4ap-skimmer PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap-skimmer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Windows: Qt's DLLs next to the program, so that it runs from the build tree with no change to PATH.
if(WIN32)
  get_target_property(KZ4AP_QMAKE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(KZ4AP_QT_BIN "${KZ4AP_QMAKE}" DIRECTORY)
  find_program(KZ4AP_WINDEPLOYQT windeployqt HINTS "${KZ4AP_QT_BIN}" NO_DEFAULT_PATH)
  if(KZ4AP_WINDEPLOYQT)
    add_custom_command(TARGET kz4ap-skimmer POST_BUILD
      COMMAND "${KZ4AP_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
              "$<TARGET_FILE:kz4ap-skimmer>"
      COMMENT "Copying Qt's DLLs next to kz4ap-skimmer")
  endif()
  # The offscreen platform too, which windeployqt leaves out: runs without an operator use it (QT_QPA_PLATFORM=offscreen), and
  # without it Qt shows an error box on the desktop and waits (decision log, 2026-11-01 and 2026-11-18).
  set(KZ4AP_QOFFSCREEN "${KZ4AP_QT_BIN}/../plugins/platforms/qoffscreen.dll")
  if(EXISTS "${KZ4AP_QOFFSCREEN}")
    add_custom_command(TARGET kz4ap-skimmer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:kz4ap-skimmer>/platforms"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${KZ4AP_QOFFSCREEN}" "$<TARGET_FILE_DIR:kz4ap-skimmer>/platforms/"
      COMMENT "Copying Qt's offscreen platform next to kz4ap-skimmer")
  endif()
endif()
