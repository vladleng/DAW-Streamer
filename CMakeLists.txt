cmake_minimum_required(VERSION 3.22)

project(DAWStreamer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule is missing. Run: git submodule update --init --recursive")
endif()

add_subdirectory(external/JUCE)

enable_testing()

add_subdirectory(plugin)
add_subdirectory(recorder)
