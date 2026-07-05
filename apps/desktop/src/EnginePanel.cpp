#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class EnginePanel final : public juce::Component {
public:
    explicit EnginePanel(WorkerService& workerToUse)
        : worker(workerToUse), job(workerToUse) {
        heading.setText("Engine Diagnostics", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        refresh.setButtonText("Refresh Engines");
        refresh.onClick = [this] {
            job.track(worker.submit("Discover engines", "engine.list",
                                    juce::var(new juce::DynamicObject())));
        };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            report.setValue(snapshot.payload);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(refresh);
        addAndMakeVisible(job);
        addAndMakeVisible(report);
        refresh.triggerClick();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(42));
        refresh.setBounds(area.removeFromTop(42).removeFromLeft(150).reduced(3));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
        report.setBounds(area.reduced(0, 6));
    }

private:
    WorkerService& worker;
    JobStatusPanel job;
    JsonResultView report;
    juce::Label heading;
    juce::TextButton refresh;
};

} // namespace

std::unique_ptr<juce::Component> createEnginePanel(WorkerService& worker) {
    return std::make_unique<EnginePanel>(worker);
}

} // namespace omnistem::desktop
