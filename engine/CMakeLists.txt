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
  src/bank/channel.cpp
)

# The new decoder's sources (first prototype); later tasks add to this list.
set(KZ4AP_VITERBI_SOURCES
  src/viterbi/morse_tree.cpp
  src/viterbi/symbol_prior.cpp
  src/viterbi/fist.cpp
  src/viterbi/observation.cpp
  src/viterbi/search.cpp
  src/viterbi/hiss.cpp
  src/viterbi/front_filter.cpp
  src/viterbi/mark_template.cpp
  src/viterbi/systematic.cpp
  src/viterbi/amplitude.cpp
  src/viterbi/long_run.cpp
  src/viterbi/newborn.cpp
)

# The generated tables (plan 1b, Ruling 72, N6): sources written by kz4ap-viterbi-tables
# (bench/src/viterbi_tables_main.cpp) and committed. The tool links a static archive of kz4ap_engine_core's objects
# only (bench/CMakeLists.txt; Ruling 76), so it builds before any of them exists; no source the tool needs may read a
# generated table (OmegaBarTable::compiled() is defined in the generated omega_bar_table.cpp itself).
set(KZ4AP_GENERATED_SOURCES
  src/viterbi/omega_bar_table.cpp
)

# Every engine source except the generated tables, as an object library; kz4ap_engine is it plus the generated tables.
add_library(kz4ap_engine_core OBJECT
  src/morse.cpp
  src/wav_reader.cpp
  src/spectrum.cpp
  src/channelizer.cpp
  src/channel_follower.cpp
  src/signal_detector.cpp
  src/classical_decoder.cpp
  src/engine.cpp
  src/cpu_time.cpp
  src/decoder_name.cpp
  src/file_iq_source.cpp
  src/stream_runner.cpp
  src/matched_front_end.cpp
  src/frequency_tracker.cpp
  src/bank_decoder.cpp
  src/viterbi_decoder.cpp
  src/segmental_decoder.cpp
  src/segmental/evidence.cpp
  src/segmental/search.cpp
  ${KZ4AP_BANK_SOURCES}
  ${KZ4AP_VITERBI_SOURCES}
)
target_include_directories(kz4ap_engine_core PUBLIC include PRIVATE src)
target_include_directories(kz4ap_engine_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/pocketfft)
target_compile_definitions(kz4ap_engine_core PRIVATE POCKETFFT_NO_MULTITHREADING)
target_compile_features(kz4ap_engine_core PUBLIC cxx_std_20)
# SLEEF's generated sleef.h and its static library (the duration fit's exp and log, src/bank/vecmath.hpp).
# Each vecmath_<set>.cpp is compiled for its instruction set and run only where vecmath.cpp finds it at run
# time; everything else stays plain x86-64.
target_include_directories(kz4ap_engine_core SYSTEM PRIVATE ${sleef_BINARY_DIR}/include)
target_link_libraries(kz4ap_engine_core PRIVATE sleef)
# The stream runner's thread.
find_package(Threads REQUIRED)
target_link_libraries(kz4ap_engine_core PUBLIC Threads::Threads)
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
  target_compile_options(kz4ap_engine_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_engine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(kz4ap_engine ${KZ4AP_GENERATED_SOURCES})
add_library(kz4ap::engine ALIAS kz4ap_engine)
target_link_libraries(kz4ap_engine PUBLIC kz4ap_engine_core)
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
  tests/channel_follower_test.cpp
  tests/signal_detector_test.cpp
  tests/classical_decoder_test.cpp
  tests/engine_test.cpp
  tests/stream_runner_test.cpp
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
# the presets windows-full-sweep and linux-full-sweep run only the tests so labeled (this one and the new decoder's
# below; ctest's -R picks one).
add_test(NAME BankFitB2a.FullSweep COMMAND kz4ap_bank_tests --gtest_filter=BankFitB2a.*)
set_tests_properties(BankFitB2a.FullSweep PROPERTIES ENVIRONMENT KZ4AP_FIT_FULL_SWEEP=1 LABELS full-sweep)

# The new decoder's tests (first prototype); later tasks add to this list.
add_executable(kz4ap_viterbi_tests
  tests/viterbi/morse_tree_test.cpp
  tests/viterbi/symbol_prior_test.cpp
  tests/viterbi/fist_test.cpp
  tests/viterbi/observation_test.cpp
  tests/viterbi/search_test.cpp
  tests/viterbi/hiss_test.cpp
  tests/viterbi/front_filter_test.cpp
  tests/viterbi/mark_template_test.cpp
  tests/viterbi/systematic_test.cpp
  tests/viterbi/amplitude_test.cpp
  tests/viterbi/long_run_test.cpp
  tests/viterbi/newborn_test.cpp
  tests/viterbi_decoder_test.cpp
  tests/segmental/segmental_test.cpp
)
target_link_libraries(kz4ap_viterbi_tests PRIVATE kz4ap::engine GTest::gtest_main)
gtest_discover_tests(kz4ap_viterbi_tests)
# The search's full-size test: the speed step and the constant-speed stations at every level from +10 to +28 dB S500
# on three seeds; about 9.5 min on the Windows PC. Labeled full-sweep: the default test presets exclude it
# (without KZ4AP_SEARCH_FULL_SWEEP the test skips), the presets windows-full-sweep and linux-full-sweep run it.
add_test(NAME Search.FullSweep COMMAND kz4ap_viterbi_tests --gtest_filter=Search.FollowsASpeedStepAtEverySnr)
set_tests_properties(Search.FullSweep PROPERTIES ENVIRONMENT KZ4AP_SEARCH_FULL_SWEEP=1 LABELS full-sweep)
# The decoder's ten minutes of noise at the ceiling, with its CPU per channel-second; about 3 min on the Windows PC.
# Labeled full-sweep as above (without KZ4AP_VITERBI_FULL_SWEEP the test runs on one minute of noise).
add_test(NAME ViterbiDecoder.FullSweep COMMAND kz4ap_viterbi_tests --gtest_filter=ViterbiDecoder.TenMinutesOfNoise)
set_tests_properties(ViterbiDecoder.FullSweep PROPERTIES ENVIRONMENT KZ4AP_VITERBI_FULL_SWEEP=1 LABELS full-sweep)
# The strong stations, +30 to +60 dB S500 at 16 positions of the keying edge within a sample (the systematic term);
# about 2.5 min on the Windows PC. Labeled full-sweep as above (without KZ4AP_VITERBI_FULL_SWEEP the test skips).
add_test(NAME ViterbiDecoder.StrongFullSweep COMMAND kz4ap_viterbi_tests
         --gtest_filter=ViterbiDecoder.StrongStationsAtEveryEdgePosition)
set_tests_properties(ViterbiDecoder.StrongFullSweep PROPERTIES ENVIRONMENT KZ4AP_VITERBI_FULL_SWEEP=1
                     LABELS full-sweep)
# The front filter's output noise measured through the channelizer: 600 s of white noise at 48 and 192 kHz (Part 15,
# item 1 of the design notes' amendment of 2026-10-25). Labeled full-sweep as above (without
# KZ4AP_FRONT_FILTER_FULL_SWEEP the test skips).
add_test(NAME OutputNoise.FullSweep COMMAND kz4ap_viterbi_tests
         --gtest_filter=OutputNoise.MeasuredThroughTheChannelizer)
set_tests_properties(OutputNoise.FullSweep PROPERTIES ENVIRONMENT KZ4AP_FRONT_FILTER_FULL_SWEEP=1
                     LABELS full-sweep)
# make_template_tables' build time at 192 kHz, under 1 s on the desktop (heuristic bound; plan 1b, Task 4). Labeled
# measurement: the default test presets exclude it (without KZ4AP_MEASUREMENT the test skips).
add_test(NAME MarkTemplate.Measurement COMMAND kz4ap_viterbi_tests --gtest_filter=MarkTemplate.BuildsQuickly)
set_tests_properties(MarkTemplate.Measurement PROPERTIES ENVIRONMENT KZ4AP_MEASUREMENT=1 LABELS measurement)
# The noise-in-Omega_bar variant's table against its direct integral (plan 1b, Task 8). The table is generated into
# build/viterbi-tables/ by kz4ap-viterbi-tables omega-bar-variant and never committed (Ruling 65); labeled needs-table,
# and reported as skipped while the file is absent (without KZ4AP_OMEGA_BAR_VARIANT_TABLE the test skips).
add_test(NAME OmegaBarVariant.NeedsTable COMMAND kz4ap_viterbi_tests
         --gtest_filter=OmegaBarVariant.TableMatchesTheDirectIntegral)
set(KZ4AP_OMEGA_BAR_VARIANT_TABLE ${PROJECT_SOURCE_DIR}/build/viterbi-tables/omega-bar-variant-v1.bin)
set_tests_properties(OmegaBarVariant.NeedsTable PROPERTIES
                     ENVIRONMENT KZ4AP_OMEGA_BAR_VARIANT_TABLE=${KZ4AP_OMEGA_BAR_VARIANT_TABLE}
                     LABELS needs-table SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]")
# The newborn integral's approximations (plan 1b, Task 9; design notes part 15, item 2, run in Task 16): the grid against
# a dense integral from noise to +85 dB S500 and the amplitude held constant under fading, written to
# build/measurements/newborn/. Labeled measurement (without KZ4AP_MEASUREMENT the test skips).
add_test(NAME Newborn.Measurement COMMAND kz4ap_viterbi_tests --gtest_filter=Newborn.ApproximationsMeasured)
set_tests_properties(Newborn.Measurement PROPERTIES
                     ENVIRONMENT "KZ4AP_MEASUREMENT=1;KZ4AP_MEASUREMENT_DIR=${PROJECT_SOURCE_DIR}/build/measurements"
                     LABELS measurement)
# The newborn integral's CPU per newborn-sample, 500 newborns on noise and on a mix of levels (plan 1b, Task 9, step 4).
# Labeled measurement as above.
add_test(NAME Newborn.CpuMeasurement COMMAND kz4ap_viterbi_tests --gtest_filter=Newborn.CpuPerNewbornSample)
set_tests_properties(Newborn.CpuMeasurement PROPERTIES ENVIRONMENT KZ4AP_MEASUREMENT=1 LABELS measurement)
