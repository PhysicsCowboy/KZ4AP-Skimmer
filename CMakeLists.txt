cmake_minimum_required(VERSION 3.25)
project(kz4ap_skimmer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

# SLEEF (Boost Software License 1.0): vectorized exp and log for the bank decoder's duration fit (Plan B task
# B2(b)). Only the static math library is built: no tests, DFT, quad, GNU ABI or scalar libraries, and no TLFloat
# (needed only by SLEEF's own tests). The engine chooses among SLEEF's bit-wise consistent functions at run time
# (engine/src/bank/vecmath.hpp), so the binaries run on every x86-64 processor.
FetchContent_Declare(sleef
  URL https://github.com/shibatch/sleef/archive/refs/tags/3.9.0.tar.gz
  URL_HASH SHA256=af60856abac08a3b5e72a8d156dd71fec1f7ac23de8ee67793f45f9edcdf0908
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(SLEEF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SLEEF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SLEEF_BUILD_DFT OFF CACHE BOOL "" FORCE)
set(SLEEF_BUILD_QUAD OFF CACHE BOOL "" FORCE)
set(SLEEF_BUILD_GNUABI_LIBS OFF CACHE BOOL "" FORCE)
set(SLEEF_BUILD_SCALAR_LIB OFF CACHE BOOL "" FORCE)
set(SLEEF_BUILD_INLINE_HEADERS OFF CACHE BOOL "" FORCE)
set(SLEEF_ENABLE_TLFLOAT OFF CACHE BOOL "" FORCE)
set(SLEEF_ENABLE_TESTER4 OFF CACHE BOOL "" FORCE)
set(SLEEF_SHOW_CONFIG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sleef)

enable_testing()
include(GoogleTest)

add_subdirectory(engine)
add_subdirectory(bench)
