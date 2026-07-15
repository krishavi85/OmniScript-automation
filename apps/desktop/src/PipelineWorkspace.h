#pragma once

#include "StudioWidgets.h"
#include <memory>

namespace omnistem::desktop {
class WorkerService;

class PipelineWorkspace final : public juce::Component {
public:
    explicit PipelineWorkspace(WorkerService& worker);
    void resized() override;

private:
    void resetDefinition();
    void appendStep();
    void submit(const juce::String& method, const juce::String& title);

    WorkerService& worker;
    int stepCount{};
    JobStatusPanel job;
    JsonResultView result;
    juce::Label heading, status;
    juce::ComboBox palette;
    juce::TextButton addStep, reset, validate, run;
    juce::TextEditor definition;
};

std::unique_ptr<juce::Component> createPipelineWorkspace(WorkerService& worker);
} // namespace omnistem::desktop
