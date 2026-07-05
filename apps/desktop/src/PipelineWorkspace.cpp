#include "PipelineWorkspace.h"
#include "StudioServices.h"

namespace omnistem::desktop {
#include "PipelineWorkspaceLayout.inc"
#include "PipelineWorkspaceSubmit.inc"

std::unique_ptr<juce::Component> createPipelineWorkspace(WorkerService& worker) {
    return std::make_unique<PipelineWorkspace>(worker);
}
} // namespace omnistem::desktop
