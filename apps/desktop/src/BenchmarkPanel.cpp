#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class BenchmarkPanel final : public juce::Component {
public:
    explicit BenchmarkPanel(WorkerService& workerToUse)
        : worker(workerToUse),
          reference("Reference WAV", false, "*.wav"),
          job(workerToUse) {
        heading.setText("Benchmark Reports", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        candidates.setMultiLine(true);
        candidates.setTextToShowWhenEmpty("One candidate WAV path per line", juce::Colours::grey);
        run.setButtonText("Run Benchmark");
        exportButton.setButtonText("Export JSON");
        exportButton.setEnabled(false);
        run.onClick = [this] { runBenchmark(); };
        exportButton.onClick = [this] { exportReport(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            lastResult = snapshot.payload;
            report.setValue(snapshot.payload);
            exportButton.setEnabled(snapshot.state == RemoteJobState::completed);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(reference);
        addAndMakeVisible(candidates);
        addAndMakeVisible(run);
        addAndMakeVisible(exportButton);
        addAndMakeVisible(job);
        addAndMakeVisible(report);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        reference.setBounds(area.removeFromTop(38));
        candidates.setBounds(area.removeFromTop(110).reduced(0, 4));
        auto row = area.removeFromTop(42);
        run.setBounds(row.removeFromLeft(140).reduced(3));
        exportButton.setBounds(row.removeFromLeft(120).reduced(3));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
        report.setBounds(area.reduced(0, 6));
    }

private:
    void runBenchmark() {
        juce::Array<juce::var> paths;
        juce::StringArray lines;
        lines.addLines(candidates.getText());
        for (auto line : lines) {
            line = line.trim();
            if (juce::File(line).existsAsFile()) paths.add(line);
        }
        if (paths.isEmpty()) {
            report.setText("Add at least one existing WAV candidate path.");
            return;
        }
        auto* params = new juce::DynamicObject();
        params->setProperty("candidates", juce::var(paths));
        if (reference.getFile().existsAsFile())
            params->setProperty("reference", reference.getFile().getFullPathName());
        job.track(worker.submit("Generate benchmark report", "benchmark.compare",
                                juce::var(params)));
    }

    void exportReport() {
        chooser = std::make_unique<juce::FileChooser>(
            "Export benchmark report", juce::File("omnistem-benchmark.json"), "*.json");
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis = juce::Component::SafePointer<BenchmarkPanel>(this)](const juce::FileChooser& selected) {
                if (safeThis == nullptr) return;
                auto file = selected.getResult();
                if (file == juce::File{}) return;
                if (!file.hasFileExtension("json")) file = file.withFileExtension("json");
                file.replaceWithText(juce::JSON::toString(safeThis->lastResult, true));
            });
    }

    WorkerService& worker;
    FilePickerRow reference;
    JobStatusPanel job;
    JsonResultView report;
    juce::Label heading;
    juce::TextEditor candidates;
    juce::TextButton run, exportButton;
    juce::var lastResult;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace

std::unique_ptr<juce::Component> createBenchmarkPanel(WorkerService& worker) {
    return std::make_unique<BenchmarkPanel>(worker);
}

} // namespace omnistem::desktop
