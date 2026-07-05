#pragma once

#include <JuceHeader.h>

namespace omnistem::desktop {
class StudioState;
class StudioAudioEngine;

int loadNormalizedStems(const juce::var& result,
                        StudioState& state,
                        StudioAudioEngine& audio,
                        juce::StringArray& errors);
}
