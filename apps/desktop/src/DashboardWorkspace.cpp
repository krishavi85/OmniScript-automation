#include "StudioServices.h"
#include "StudioWidgets.h"

#include <JuceHeader.h>
#include <memory>

namespace omnistem::desktop {

namespace {

class DashboardWorkspace final : public juce::Component,
                                 private juce::Timer,
                                 private juce::ChangeListener {
public:
    DashboardWorkspace(StudioState& stateToUse,
                       WorkerService& workerToUse,
                       LogStore& logsToUse)
        : state(stateToUse), worker(workerToUse), logs(logsToUse), status(workerToUse) {
        title.setText("OmniStem Studio Dashboard", juce::dontSendNotification);
        title.setFont(juce::FontOptions(26.0f, juce::Font::bold));
        source.setJustificationType(juce::Justification::centredLeft);
        output.setJustificationType(juce::Justification::centredLeft);
        api.setJustificationType(juce::Justification::centredLeft);
        summary.setMultiLine(true);
        summary.setReadOnly(true);
        summary.setScrollbarsShown(true);
        summary.setColour(juce::TextEditor::backgroundColourId,
                          juce::Colour::fromRGB(16, 18, 24));
        checkWorker.setButtonText("Check Worker");
        revealOutput.setButtonText("Open Output Folder");

        checkWorker.onClick = [this] {
            status.track(worker.submit("Worker health check", "health.check",
                                       juce::var(new juce::DynamicObject())));
        };
        revealOutput.onClick = [this] {
            state.getOutputDirectory().revealToUser();
        };
        status.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            if (snapshot.state == RemoteJobState::completed)
                health.setText("Worker: online", juce::dontSendNotification);
            else
                health.setText("Worker: " + snapshot.message, juce::dontSendNotification);
            refresh();
        };

        addAndMakeVisible(title);
        addAndMakeVisible(source);
        addAndMakeVisible(output);
        addAndMakeVisible(api);
        addAndMakeVisible(health);
        addAndMakeVisible(checkWorker);
        addAndMakeVisible(revealOutput);
        addAndMakeVisible(status);
        addAndMakeVisible(summary);

        state.addChangeListener(this);
        worker.addChangeListener(this);
        logs.addChangeListener(this);
        startTimerHz(2);
        refresh();
    }

    ~DashboardWorkspace() override {
        stopTimer();
        state.removeChangeListener(this);
        worker.removeChangeListener(this);
        logs.removeChangeListener(this);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16);
        title.setBounds(area.removeFromTop(42));
        source.setBounds(area.removeFromTop(30));
        output.setBounds(area.removeFromTop(30));
        api.setBounds(area.removeFromTop(30));
        health.setBounds(area.removeFromTop(30));
        auto buttons = area.removeFromTop(42);
        checkWorker.setBounds(buttons.removeFromLeft(150).reduced(3));
        revealOutput.setBounds(buttons.removeFromLeft(180).reduced(3));
        status.setBounds(area.removeFromTop(58).reduced(0, 4));
        summary.setBounds(area.reduced(0, 8));
    }

private:
    void timerCallback() override { refresh(); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    void refresh() {
        const auto audio = state.getCurrentAudio();
        source.setText("Source: " + (audio.existsAsFile() ? audio.getFullPathName() : "No audio loaded"),
                       juce::dontSendNotification);
        output.setText("Output: " + state.getOutputDirectory().getFullPathName(),
                       juce::dontSendNotification);
        api.setText("Worker API: " + state.getApiUrl(), juce::dontSendNotification);

        const auto jobs = worker.snapshots();
        int queued = 0, running = 0, completed = 0, failed = 0, cancelled = 0;
        for (const auto& job : jobs) {
            switch (job.state) {
                case RemoteJobState::queued: ++queued; break;
                case RemoteJobState::running: ++running; break;
                case RemoteJobState::completed: ++completed; break;
                case RemoteJobState::failed: ++failed; break;
                case RemoteJobState::cancelled: ++cancelled; break;
            }
        }

        juce::String text;
        text << "Project: " << juce::String(state.project().name) << "\n";
        text << "Project ID: " << juce::String(state.project().id) << "\n";
        text << "Stems in project: " << static_cast<int>(state.project().stems.size()) << "\n";
        text << "Note objects: " << static_cast<int>(state.project().notes.size()) << "\n";
        text << "Spectral edits: " << static_cast<int>(state.project().masks.size()) << "\n\n";
        text << "Jobs — queued: " << queued << ", running: " << running
             << ", completed: " << completed << ", failed: " << failed
             << ", cancelled: " << cancelled << "\n";
        text << "Log records: " << static_cast<int>(logs.entries().size()) << "\n\n";
        text << "Recent jobs:\n";
        for (int index = 0; index < juce::jmin(8, static_cast<int>(jobs.size())); ++index) {
            const auto& job = jobs[static_cast<std::size_t>(index)];
            text << "• " << job.title << " — " << toString(job.state)
                 << " — " << job.message << "\n";
        }
        summary.setText(text, false);
    }

    StudioState& state;
    WorkerService& worker;
    LogStore& logs;
    juce::Label title;
    juce::Label source;
    juce::Label output;
    juce::Label api;
    juce::Label health;
    juce::TextButton checkWorker;
    juce::TextButton revealOutput;
    JobStatusPanel status;
    juce::TextEditor summary;
};

} // namespace

std::unique_ptr<juce::Component> makeDashboardWorkspace(
    StudioState& state, WorkerService& worker, LogStore& logs) {
    return std::make_unique<DashboardWorkspace>(state, worker, logs);
}

} // namespace omnistem::desktop
