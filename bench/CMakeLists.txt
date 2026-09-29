add_library(kz4ap_bench_lib
  src/cpu_time.cpp
  src/labels.cpp
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

add_executable(kz4ap_bench_tests
  tests/cpu_time_test.cpp
  tests/labels_test.cpp
  tests/scoring_test.cpp
)
target_link_libraries(kz4ap_bench_tests PRIVATE kz4ap_bench_lib GTest::gtest_main)
gtest_discover_tests(kz4ap_bench_tests)
