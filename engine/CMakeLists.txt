add_library(kz4ap_engine
  src/morse.cpp
  src/wav_reader.cpp
)
add_library(kz4ap::engine ALIAS kz4ap_engine)
target_include_directories(kz4ap_engine PUBLIC include PRIVATE src)
target_compile_features(kz4ap_engine PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(kz4ap_engine PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap_engine_tests
  tests/morse_test.cpp
  tests/wav_reader_test.cpp
)
target_link_libraries(kz4ap_engine_tests PRIVATE kz4ap::engine GTest::gtest_main)
gtest_discover_tests(kz4ap_engine_tests)
