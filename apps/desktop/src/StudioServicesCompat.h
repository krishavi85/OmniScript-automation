#pragma once

#include <JuceHeader.h>

namespace juce {

class OmniStemApplicationProperties final : public ApplicationProperties {
public:
    PropertiesFile& getUserSettings() {
        auto* file = ApplicationProperties::getUserSettings();
        jassert(file != nullptr);
        return *file;
    }
};

} // namespace juce

#define ApplicationProperties OmniStemApplicationProperties
