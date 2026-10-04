# The bank decoder's sources (milestone 2c); later tasks add to this list.
set(KZ4AP_BANK_SOURCES
  src/bank/filters.cpp
  src/bank/noise.cpp
  src/bank/keying.cpp
  src/bank/fit.cpp
  src/bank/vecmath.cpp
  src/bank/vecmath_sse2.cpp
  src/bank/vecmath_avx.cpp
  src/bank/vecmath_avx2.cpp
  src/bank/vecmath_avx512.cpp
  src/bank/periodicity.cpp
  src/bank/selection.cpp
  src/bank/timing.cpp
  src/bank/channel.cpp
)

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
  src/bank_decoder.cpp
  ${KZ4AP_BANK_SOURCES}
)
add_library(kz4ap::engine ALIAS kz4ap_engine)
target_include_directories(kz4ap_engine PUBLIC include PRIVATE src)
target_include_directories(kz4ap_engine SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/pocketfft)
target_compile_definitions(kz4ap_engine PRIVATE POCKETFFT_NO_MULTITHREADING)
target_compile_features(kz4ap_engine PUBLIC cxx_std_20)
# SLEEF's generated sleef.h and its static library (the duration fit's exp and log, src/bank/vecmath.hpp).
# Each vecmath_<set>.cpp is compiled for its instruction set and run only where vecmath.cpp finds it at run
# time; everything else stays plain x86-64.
target_include_directories(kz4ap_engine SYSTEM PRIVATE ${sleef_BINARY_DIR}/include)
target_link_libraries(kz4ap_engine PRIVATE sleef)
if(MSVC)
  set_source_files_properties(src/bank/vecmath_avx.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX)
  set_source_files_properties(src/bank/vecmath_avx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
  # As SLEEF builds its own AVX-512 code with MSVC: AVX2 code generation and AVX-512F intrinsics only (/arch:AVX512
  # would also emit AVX-512 CD, BW, DQ and VL instructions, which vecmath.cpp does not check for).
  set_source_files_properties(src/bank/vecmath_avx512.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/D__AVX512F__")
else()
  set_source_files_properties(src/bank/vecmath_avx.cpp PROPERTIES COMPILE_OPTIONS -mavx)
  set_source_files_properties(src/bank/vecmath_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
  set_source_files_properties(src/bank/vecmath_avx512.cpp PROPERTIES COMPILE_OPTIONS -mavx512f)
endif()
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
  tests/bank/filters_test.cpp
  tests/bank/noise_test.cpp
  tests/bank/keying_test.cpp
  tests/bank/fit_test.cpp
  tests/bank/periodicity_test.cpp
  tests/bank/selection_test.cpp
  tests/bank/channel_test.cpp
  tests/bank_decoder_test.cpp
)
target_link_libraries(kz4ap_bank_tests PRIVATE kz4ap::engine nlohmann_json::nlohmann_json GTest::gtest_main)
# fit_test.cpp compares the fit's exp and log implementations (src/bank/vecmath.hpp).
target_include_directories(kz4ap_bank_tests PRIVATE src)
target_compile_definitions(kz4ap_bank_tests PRIVATE
  KZ4AP_BANK_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bank")
gtest_discover_tests(kz4ap_bank_tests)
# Plan B task B2(a): the fit's frozen-copy sweeps at their full sizes (10^6 observations per configuration at
# every grid point, 2000 random fits per configuration and number of refinement steps), about 6 min on the
# Linux machine and 41 min on the Windows PC (since B2(b)). Labeled full-sweep: the default test presets exclude it,
# the presets windows-full-sweep and linux-full-sweep run only it.
add_test(NAME BankFitB2a.FullSweep COMMAND kz4ap_bank_tests --gtest_filter=BankFitB2a.*)
set_tests_properties(BankFitB2a.FullSweep PROPERTIES ENVIRONMENT KZ4AP_FIT_FULL_SWEEP=1 LABELS full-sweep)
