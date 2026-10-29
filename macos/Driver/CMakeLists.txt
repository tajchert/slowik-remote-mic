cmake_minimum_required(VERSION 3.16)
project(SlowikMic CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(CTest)

# Host test for the CoreAudio-free ring buffer.
add_executable(loopback_ring_test test/LoopbackRingTest.cpp)
add_test(NAME loopback_ring COMMAND loopback_ring_test)

if(APPLE)
  include(FetchContent)
  FetchContent_Declare(libASPL
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG v3.1.2
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(libASPL)

  set(DRIVER_VERSION "0.1.0")
  add_library(SlowikMic MODULE src/Driver.cpp)
  target_link_libraries(SlowikMic PRIVATE libASPL)
  set_target_properties(SlowikMic PROPERTIES
    OUTPUT_NAME "SlowikMic"
    BUNDLE true
    BUNDLE_EXTENSION "driver"
    PREFIX ""
    SUFFIX ""
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "SlowikMic"
    MACOSX_BUNDLE_BUNDLE_VERSION "${DRIVER_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${DRIVER_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Slowik authors, MIT"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.slowik.SlowikMic")
endif()
