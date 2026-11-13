cmake_minimum_required(VERSION 3.16)
project(dedup_snapshot_store CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DSS_SANITIZE "Build with ASan + UBSan" OFF)
if(DSS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(dss src/sha256.cpp src/chunker.cpp src/store.cpp)
target_include_directories(dss PUBLIC include)
target_compile_options(dss PRIVATE -Wall -Wextra -Wpedantic)

add_executable(dss_cli src/main.cpp)
set_target_properties(dss_cli PROPERTIES OUTPUT_NAME dss)
target_link_libraries(dss_cli PRIVATE dss)

option(DSS_BUILD_TESTS "Build unit tests" ON)
if(DSS_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_executable(dss_tests tests/store_test.cpp)
  target_link_libraries(dss_tests PRIVATE dss GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(dss_tests)
endif()
