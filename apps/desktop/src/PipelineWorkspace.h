#pragma once

#include <JuceHeader.h>
#include <memory>

namespace omnistem::desktop {
class WorkerService;
std::unique_ptr<juce::Component> createPipelineWorkspace(WorkerService& worker);
}
