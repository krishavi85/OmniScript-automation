#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class PreferencesPanel final : public juce::Component {
public:
    PreferencesPanel(StudioState& stateToUse, WorkerService& workerToUse)
        : state(stateToUse), worker(workerToUse),
          output("Default output", true), job(workerToUse) {
        heading.setText("Settings", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        apiLabel.setText("Worker API URL", juce::dontSendNotification);
        api.setText(state.getApiUrl());
        output.setFile(state.getOutputDirectory());
        save.setButtonText("Save Settings");
        test.setButtonText("Test Connection");
        save.onClick = [this] {
            state.setApiUrl(api.getText());
            state.setOutputDirectory(output.getFile());
            state.saveSettings();
            message.setText("Settings saved", juce::dontSendNotification);
        };
        test.onClick = [this] {
            job.track(worker.submit("Test worker connection", "health.check",
                                    juce::var(new juce::DynamicObject())));
        };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            message.setText(snapshot.state == RemoteJobState::completed
                                ? "Worker connection successful"
                                : "Worker connection failed: " + snapshot.message,
                            juce::dontSendNotification);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(apiLabel);
        addAndMakeVisible(api);
        addAndMakeVisible(output);
        addAndMakeVisible(save);
        addAndMakeVisible(test);
        addAndMakeVisible(message);
        addAndMakeVisible(job);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16);
        heading.setBounds(area.removeFromTop(42));
        auto row = area.removeFromTop(38);
        apiLabel.setBounds(row.removeFromLeft(140));
        api.setBounds(row.reduced(3));
        output.setBounds(area.removeFromTop(38));
        row = area.removeFromTop(44);
        save.setBounds(row.removeFromLeft(140).reduced(3));
        test.setBounds(row.removeFromLeft(140).reduced(3));
        message.setBounds(area.removeFromTop(34));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
    }

private:
    StudioState& state;
    WorkerService& worker;
    FilePickerRow output;
    JobStatusPanel job;
    juce::Label heading, apiLabel, message;
    juce::TextEditor api;
    juce::TextButton save, test;
};

} // namespace

std::unique_ptr<juce::Component> createPreferencesPanel(
    StudioState& state, WorkerService& worker) {
    return std::make_unique<PreferencesPanel>(state, worker);
}

} // namespace omnistem::desktop
