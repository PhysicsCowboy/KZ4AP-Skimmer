# The bank decoder's sources (milestone 2c); later tasks add to this list.
set(KZ4AP_BANK_SOURCES)

add_library(kz4ap_engine
  src/morse.cpp
  src/wav_reader.cpp
  src/spectrum.cpp
  src/channelizer.cpp
  src/signal_detector.cpp
  src/classical_decoder.cpp
  src/engine.cpp
  src/matched_front_end.cpp
  src/frequency_tracker.cpp
  ${KZ4AP_BANK_SOURCES}
)
add_library(kz4ap::engine ALIAS kz4ap_engine)
target_include_directories(kz4ap_engine PUBLIC include PRIVATE src)
target_include_directories(kz4ap_engine SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/pocketfft)
target_compile_definitions(kz4ap_engine PRIVATE POCKETFFT_NO_MULTITHREADING)
target_compile_features(kz4ap_engine PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(kz4ap_engine PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap_engine_tests
  tests/morse_test.cpp
  tests/wav_reader_test.cpp
  tests/spectrum_test.cpp
  tests/channelizer_test.cpp
  tests/signal_detector_test.cpp
  tests/classical_decoder_test.cpp
  tests/engine_test.cpp
  tests/matched_front_end_test.cpp
  tests/frequency_tracker_test.cpp
)
target_link_libraries(kz4ap_engine_tests PRIVATE kz4ap::engine GTest::gtest_main)
gtest_discover_tests(kz4ap_engine_tests)

# The bank decoder's tests. nlohmann/json is a test-only dependency (golden values); the engine
# library does not link it.
add_executable(kz4ap_bank_tests
  tests/bank/config_test.cpp
)
target_link_libraries(kz4ap_bank_tests PRIVATE kz4ap::engine nlohmann_json::nlohmann_json GTest::gtest_main)
target_compile_definitions(kz4ap_bank_tests PRIVATE
  KZ4AP_BANK_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bank")
gtest_discover_tests(kz4ap_bank_tests)
