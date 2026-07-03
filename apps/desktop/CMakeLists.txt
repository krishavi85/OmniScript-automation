add_executable(OmniStemCLI src/main.cpp)
target_link_libraries(OmniStemCLI PRIVATE OmniStemCore)

if(MSVC)
    target_compile_options(OmniStemCLI PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(OmniStemCLI PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OMNISTEM_BUILD_JUCE_APP)
    if(NOT OMNISTEM_JUCE_SOURCE_DIR)
        message(FATAL_ERROR "Set OMNISTEM_JUCE_SOURCE_DIR to a JUCE checkout")
    endif()

    add_subdirectory(${OMNISTEM_JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/juce)

    juce_add_gui_app(OmniStemStudio
        PRODUCT_NAME "OmniStem Studio"
        COMPANY_NAME "OmniStem"
        BUNDLE_ID "studio.omnistem.desktop"
    )

    juce_generate_juce_header(OmniStemStudio)
    target_sources(OmniStemStudio PRIVATE src/JuceApp.cpp)
    target_link_libraries(OmniStemStudio PRIVATE
        OmniStemCore
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    )
    target_compile_definitions(OmniStemStudio PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_WASAPI=1
        JUCE_ASIO=$<BOOL:${OMNISTEM_ENABLE_ASIO}>
    )
endif()
