cmake_minimum_required(VERSION 3.24)
project(OmniStemStudio VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OMNISTEM_BUILD_JUCE_APP "Build the JUCE desktop DAW application" OFF)
option(OMNISTEM_ENABLE_ASIO "Enable JUCE ASIO support when a compatible ASIO SDK is available" OFF)
set(OMNISTEM_JUCE_SOURCE_DIR "" CACHE PATH "Path to a JUCE source checkout")

find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)

enable_testing()
add_subdirectory(packages/core)
add_subdirectory(apps/desktop)
