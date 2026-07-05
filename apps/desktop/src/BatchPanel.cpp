#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class BatchPanel final : public juce::Component,
                         private juce::Timer {
public:
    BatchPanel(StudioState& stateToUse, WorkerService& workerToUse)
        : state(stateToUse), worker(workerToUse),
          input("Input folder", true), output("Output folder", true) {
        heading.setText("Batch Processing", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        output.setFile(state.getOutputDirectory());
        recursive.setButtonText("Include subfolders");
        recursive.setToggleState(true, juce::dontSendNotification);
        start.setButtonText("Queue Batch");
        cancel.setButtonText("Cancel All");
        cancel.setEnabled(false);
        report.setMultiLine(true);
        report.setReadOnly(true);
        report.setScrollbarsShown(true);
        start.onClick = [this] { queueBatch(); };
        cancel.onClick = [this] {
            for (const auto& id : jobIds) worker.cancel(id);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(input);
        addAndMakeVisible(output);
        addAndMakeVisible(recursive);
        addAndMakeVisible(start);
        addAndMakeVisible(cancel);
        addAndMakeVisible(summary);
        addAndMakeVisible(report);
        startTimerHz(4);
    }

    ~BatchPanel() override { stopTimer(); }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        input.setBounds(area.removeFromTop(38));
        output.setBounds(area.removeFromTop(38));
        auto row = area.removeFromTop(42);
        recursive.setBounds(row.removeFromLeft(180).reduced(3));
        start.setBounds(row.removeFromLeft(130).reduced(3));
        cancel.setBounds(row.removeFromLeft(110).reduced(3));
        summary.setBounds(area.removeFromTop(36));
        report.setBounds(area.reduced(0, 6));
    }

private:
    void queueBatch() {
        const auto folder = input.getFile();
        if (!folder.isDirectory()) {
            report.setText("Choose a valid input folder.", false);
            return;
        }
        const auto outputFolder = output.getFile();
        outputFolder.createDirectory();
        juce::Array<juce::File> files;
        folder.findChildFiles(files, juce::File::findFiles,
                              recursive.getToggleState(),
                              "*.wav;*.flac;*.aiff;*.aif;*.mp3;*.ogg;*.m4a;*.caf");
        jobIds.clear();
        for (const auto& file : files) {
            auto* params = new juce::DynamicObject();
            params->setProperty("source", file.getFullPathName());
            params->setProperty("outputDir",
                outputFolder.getChildFile(file.getFileNameWithoutExtension()).getFullPathName());
            params->setProperty("mode", "standard");
            params->setProperty("engine", "demucs");
            params->setProperty("model", "htdemucs_ft");
            juce::Array<juce::var> stems;
            stems.add("vocals");
            stems.add("instrumental");
            params->setProperty("stems", juce::var(stems));
            jobIds.push_back(worker.submit("Batch: " + file.getFileName(),
                                           "mode.run", juce::var(params)));
        }
        cancel.setEnabled(!jobIds.empty());
        timerCallback();
    }

    void timerCallback() override {
        int active = 0, done = 0, failed = 0;
        juce::String text;
        for (const auto& id : jobIds) {
            const auto snapshot = worker.snapshot(id);
            if (!snapshot) continue;
            if (snapshot->state == RemoteJobState::queued
                || snapshot->state == RemoteJobState::running) ++active;
            if (snapshot->state == RemoteJobState::completed) ++done;
            if (snapshot->state == RemoteJobState::failed) ++failed;
            text << snapshot->title << " — " << toString(snapshot->state)
                 << " — " << snapshot->message << "\n";
        }
        summary.setText("Queued: " + juce::String(jobIds.size())
                        + " | Active: " + juce::String(active)
                        + " | Complete: " + juce::String(done)
                        + " | Failed: " + juce::String(failed),
                        juce::dontSendNotification);
        report.setText(text, false);
        cancel.setEnabled(active > 0);
    }

    StudioState& state;
    WorkerService& worker;
    FilePickerRow input, output;
    juce::Label heading, summary;
    juce::ToggleButton recursive;
    juce::TextButton start, cancel;
    juce::TextEditor report;
    std::vector<juce::String> jobIds;
};

} // namespace

std::unique_ptr<juce::Component> createBatchPanel(StudioState& state,
                                                   WorkerService& worker) {
    return std::make_unique<BatchPanel>(state, worker);
}

} // namespace omnistem::desktop
