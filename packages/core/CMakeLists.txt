add_library(OmniStemCore STATIC src/OmniStemCore.cpp)
target_include_directories(OmniStemCore PUBLIC include)
target_link_libraries(OmniStemCore PUBLIC Threads::Threads)
