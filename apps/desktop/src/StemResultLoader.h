#pragma once

#include <JuceHeader.h>

namespace omnistem::desktop {
class StudioState;
class StudioAudioEngine;

int loadNormalizedStems(const juce::var& result,
                        StudioState& state,
                        StudioAudioEngine& audio,
                        juce::StringArray& errors);

void registerAutomaticStemTarget(StudioAudioEngine* audio);
void unregisterAutomaticStemTarget(StudioAudioEngine* audio);
int automaticallyLoadNormalizedStems(const juce::var& result,
                                     juce::StringArray& errors);
}
