add_library(kz4ap_bench_lib
  src/bank_json.cpp
  src/channel_recorder.cpp
  src/cpu_time.cpp
  src/labels.cpp
  src/noise_mask.cpp
  src/report.cpp
  src/scoring.cpp
)
target_include_directories(kz4ap_bench_lib PUBLIC src)
target_link_libraries(kz4ap_bench_lib PUBLIC kz4ap::engine nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(kz4ap_bench_lib PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_bench_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap-bench src/main.cpp)
target_link_libraries(kz4ap-bench PRIVATE kz4ap_bench_lib)

# Decodes recorded oracle channels with the C++ bank decoder into the prototype's decoded-file format.
add_executable(kz4ap-bank-replay src/replay.cpp)
target_link_libraries(kz4ap-bank-replay PRIVATE kz4ap_bench_lib)
if(MSVC)
  target_compile_options(kz4ap-bank-replay PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap-bank-replay PRIVATE -Wall -Wextra -Wpedantic)
  find_package(Threads REQUIRED)
  target_link_libraries(kz4ap-bank-replay PRIVATE Threads::Threads)
endif()

# Measures the spectrum noise estimate's mask: the mask-bias table b_mask,k in white noise and the mask's acceptance on
# a stream (Plan B, B4b).
add_executable(kz4ap-noise-mask src/noise_mask_main.cpp)
target_link_libraries(kz4ap-noise-mask PRIVATE kz4ap_bench_lib)
if(MSVC)
  target_compile_options(kz4ap-noise-mask PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap-noise-mask PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap_bench_tests
  tests/bank_json_test.cpp
  tests/channel_recorder_test.cpp
  tests/cpu_time_test.cpp
  tests/labels_test.cpp
  tests/noise_mask_test.cpp
  tests/report_test.cpp
  tests/scoring_test.cpp
)
target_link_libraries(kz4ap_bench_tests PRIVATE kz4ap_bench_lib GTest::gtest_main)
# bank_json_test reads the bank decoder's golden values with the engine tests' helper.
target_include_directories(kz4ap_bench_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/tests/bank)
target_compile_definitions(kz4ap_bench_tests PRIVATE
  KZ4AP_BANK_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/engine/tests/data/bank")
gtest_discover_tests(kz4ap_bench_tests)

# kz4ap-bank-replay refuses a --set rekey_after_s or rekey_timeout_s that is not positive once, before it reads the
# suite: exit status 1 and one message (tests/replay_refusal.cmake; it needs no suite and writes nothing).
add_test(NAME BankReplay.RefusesANonPositiveReKeyTimeOnceBeforeReadingTheSuite
         COMMAND ${CMAKE_COMMAND} -DREPLAY=$<TARGET_FILE:kz4ap-bank-replay>
                 -DSUITE=${CMAKE_CURRENT_BINARY_DIR}/no-such-suite
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_refusal.cmake)
