cmake_minimum_required(VERSION 3.14)
project(ecg_processing_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(testProcessing 
    testProcessing.cpp 
    ../ecgProcessing.cpp
    /usr/local/include/arduinoFFT.cpp
)

target_include_directories(testProcessing PRIVATE
    ..
    /usr/local/include
)

target_link_libraries(testProcessing
    GTest::gtest_main
    m
)

include(GoogleTest)
gtest_discover_tests(testProcessing)