add_library(OmniStemCore STATIC src/OmniStemCore.cpp)

target_include_directories(OmniStemCore PUBLIC include)
target_link_libraries(OmniStemCore PUBLIC Threads::Threads)
target_compile_definitions(OmniStemCore PUBLIC OMNISTEM_VERSION="${PROJECT_VERSION}")

if(SQLite3_FOUND)
    target_link_libraries(OmniStemCore PRIVATE SQLite::SQLite3)
    target_compile_definitions(OmniStemCore PRIVATE OMNISTEM_HAS_SQLITE=1)
else()
    target_compile_definitions(OmniStemCore PRIVATE OMNISTEM_HAS_SQLITE=0)
endif()

if(MSVC)
    target_compile_options(OmniStemCore PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(OmniStemCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
