cmake_minimum_required(VERSION 3.24)
project(OmniStemDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(OmniStemDesktop src/main.cpp)

target_compile_definitions(OmniStemDesktop PRIVATE
    OMNISTEM_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(OmniStemDesktop PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(OmniStemDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
