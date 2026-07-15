if(NOT OMNISTEM_CLAP_INCLUDE_DIR)
    message(FATAL_ERROR "OMNISTEM_CLAP_INCLUDE_DIR is required")
endif()

add_executable(OmniStemClapRuntime
    src/ClapRuntime.cpp
)
target_include_directories(OmniStemClapRuntime PRIVATE
    ${OMNISTEM_CLAP_INCLUDE_DIR}
)
target_compile_features(OmniStemClapRuntime PRIVATE cxx_std_20)
if(UNIX AND NOT APPLE)
    target_link_libraries(OmniStemClapRuntime PRIVATE dl)
endif()
