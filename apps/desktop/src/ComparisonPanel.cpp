#include "StudioAudioEngine.h"
#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class ComparisonPanel final : public juce::Component,
                              private juce::Timer {
public:
    ComparisonPanel(WorkerService& workerToUse,
                    StudioAudioEngine& audioToUse,
                    juce::AudioFormatManager& formats)
        : worker(workerToUse), audio(audioToUse),
          fileA("Version A", false, "*.wav;*.flac;*.aiff;*.mp3;*.ogg;*.m4a"),
          fileB("Version B", false, "*.wav;*.flac;*.aiff;*.mp3;*.ogg;*.m4a"),
          reference("Reference", false, "*.wav;*.flac;*.aiff"),
          waveformA(formats), waveformB(formats), job(workerToUse) {
        heading.setText("Comparison Lab", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        useA.setButtonText("Listen A");
        useB.setButtonText("Listen B");
        useA.setRadioGroupId(1);
        useB.setRadioGroupId(1);
        useA.setToggleState(true, juce::dontSendNotification);
        play.setButtonText("Play Synchronized");
        stop.setButtonText("Stop");
        benchmark.setButtonText("Run Benchmark");
        position.setRange(0.0, 1.0, 0.001);
        position.setSliderStyle(juce::Slider::LinearHorizontal);
        position.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

        fileA.onFileChanged = [this](const juce::File& file) {
            waveformA.setFile(file);
            loadPair();
        };
        fileB.onFileChanged = [this](const juce::File& file) {
            waveformB.setFile(file);
            loadPair();
        };
        useA.onClick = [this] { audio.selectComparisonB(false); };
        useB.onClick = [this] { audio.selectComparisonB(true); };
        play.onClick = [this] { audio.setMode(StudioAudioEngine::Mode::comparison); audio.play(); };
        stop.onClick = [this] { audio.stop(); };
        position.onDragEnd = [this] { audio.setPosition(position.getValue() * audio.getLength()); };
        benchmark.onClick = [this] { runBenchmark(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            result.setValue(snapshot.payload);
        };

        for (auto* component : {static_cast<juce::Component*>(&heading), &fileA, &fileB,
                                &reference, &useA, &useB, &play, &stop, &benchmark,
                                &position, &waveformA, &waveformB, &job, &result})
            addAndMakeVisible(component);
        startTimerHz(20);
    }

    ~ComparisonPanel() override { stopTimer(); }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        fileA.setBounds(area.removeFromTop(38));
        fileB.setBounds(area.removeFromTop(38));
        reference.setBounds(area.removeFromTop(38));
        auto row = area.removeFromTop(42);
        useA.setBounds(row.removeFromLeft(100).reduced(3));
        useB.setBounds(row.removeFromLeft(100).reduced(3));
        play.setBounds(row.removeFromLeft(150).reduced(3));
        stop.setBounds(row.removeFromLeft(90).reduced(3));
        benchmark.setBounds(row.removeFromLeft(140).reduced(3));
        position.setBounds(area.removeFromTop(30));
        auto waves = area.removeFromTop(170);
        waveformA.setBounds(waves.removeFromLeft(waves.getWidth() / 2).reduced(3));
        waveformB.setBounds(waves.reduced(3));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
        result.setBounds(area.reduced(0, 6));
    }

private:
    void timerCallback() override {
        const auto length = audio.getLength();
        const auto normalized = length > 0.0 ? audio.getPosition() / length : 0.0;
        if (!position.isMouseButtonDown()) position.setValue(normalized, juce::dontSendNotification);
        waveformA.setPlayhead(normalized);
        waveformB.setPlayhead(normalized);
    }

    void loadPair() {
        if (!fileA.getFile().existsAsFile() || !fileB.getFile().existsAsFile()) return;
        juce::String error;
        if (!audio.loadComparison(fileA.getFile(), fileB.getFile(), error))
            result.setText(error);
    }

    void runBenchmark() {
        if (!fileA.getFile().existsAsFile() || !fileB.getFile().existsAsFile()) {
            result.setText("Choose both comparison files first.");
            return;
        }
        auto* params = new juce::DynamicObject();
        juce::Array<juce::var> candidates;
        candidates.add(fileA.getFile().getFullPathName());
        candidates.add(fileB.getFile().getFullPathName());
        params->setProperty("candidates", juce::var(candidates));
        if (reference.getFile().existsAsFile())
            params->setProperty("reference", reference.getFile().getFullPathName());
        job.track(worker.submit("Compare audio versions", "benchmark.compare", juce::var(params)));
    }

    WorkerService& worker;
    StudioAudioEngine& audio;
    FilePickerRow fileA, fileB, reference;
    WaveformView waveformA, waveformB;
    JobStatusPanel job;
    JsonResultView result;
    juce::Label heading;
    juce::ToggleButton useA, useB;
    juce::TextButton play, stop, benchmark;
    juce::Slider position;
};

} // namespace

std::unique_ptr<juce::Component> createComparisonPanel(
    WorkerService& worker, StudioAudioEngine& audio, juce::AudioFormatManager& formats) {
    return std::make_unique<ComparisonPanel>(worker, audio, formats);
}

} // namespace omnistem::desktop
