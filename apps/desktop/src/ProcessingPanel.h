#pragma once

#include <JuceHeader.h>
#include <memory>

namespace omnistem::desktop {
class StudioState;
class WorkerService;
class StudioAudioEngine;

std::unique_ptr<juce::Component> createProcessingPanel(
    StudioState&, WorkerService&, StudioAudioEngine&, juce::AudioFormatManager&);
}
