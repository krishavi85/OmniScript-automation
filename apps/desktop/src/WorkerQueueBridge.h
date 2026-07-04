#pragma once

#include <JuceHeader.h>

namespace omnistem::desktop {

class WorkerQueueBridge final {
public:
    static juce::File queueDirectory() {
        const auto configured = juce::SystemStats::getEnvironmentVariable("OMNISTEM_WORKER_QUEUE", {});
        if (configured.isNotEmpty())
            return juce::File(configured);
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("OmniStemStudio")
            .getChildFile("worker-queue");
    }
};

} // namespace omnistem::desktop
