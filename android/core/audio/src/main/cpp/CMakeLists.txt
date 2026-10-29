cmake_minimum_required(VERSION 3.22.1)
project(slowik_audio LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)

add_library(slowik_audio SHARED engine.cpp jni.cpp)
target_compile_features(slowik_audio PRIVATE cxx_std_17)
target_compile_options(slowik_audio PRIVATE -Wall -Wextra)
target_link_libraries(slowik_audio oboe::oboe log)
# Android 15+ requires 16 KB page alignment
target_link_options(slowik_audio PRIVATE -Wl,-z,max-page-size=16384)
