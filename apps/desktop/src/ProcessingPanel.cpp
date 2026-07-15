#include "ProcessingPanel.h"
#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class ProcessingPanel final : public juce::Component {
public:
    ProcessingPanel(StudioState& stateToUse,
                    WorkerService& workerToUse,
                    StudioAudioEngine& audioToUse,
                    juce::AudioFormatManager& formats)
        : state(stateToUse), worker(workerToUse), audio(audioToUse),
          source("Source audio", false, "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg;*.m4a"),
          output("Output folder", true), waveform(formats), job(workerToUse) {
        heading.setText("AI Stem Processing", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        mode.addItemList({"standard", "auto"}, 1);
        mode.setSelectedId(1);
        run.setButtonText("Run");
        source.setFile(state.getCurrentAudio());
        output.setFile(state.getOutputDirectory());
        source.onFileChanged = [this](const juce::File& file) {
            if (!file.existsAsFile()) return;
            state.setCurrentAudio(file);
            waveform.setFile(file);
            juce::String error;
            if (!audio.loadPreview(file, error)) result.setText(error);
        };
        output.onFileChanged = [this](const juce::File& file) {
            if (file != juce::File{}) state.setOutputDirectory(file);
        };
        run.onClick = [this] { submit(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            result.setValue(snapshot.payload);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(source);
        addAndMakeVisible(output);
        addAndMakeVisible(mode);
        addAndMakeVisible(run);
        addAndMakeVisible(job);
        addAndMakeVisible(waveform);
        addAndMakeVisible(result);
        if (state.getCurrentAudio().existsAsFile()) waveform.setFile(state.getCurrentAudio());
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        heading.setBounds(area.removeFromTop(38));
        source.setBounds(area.removeFromTop(38));
        output.setBounds(area.removeFromTop(38));
        auto controls = area.removeFromTop(40);
        mode.setBounds(controls.removeFromLeft(180).reduced(3));
        run.setBounds(controls.removeFromLeft(100).reduced(3));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
        waveform.setBounds(area.removeFromTop(area.getHeight() / 2).reduced(0, 4));
        result.setBounds(area.reduced(0, 4));
    }

private:
    void submit() {
        const auto file = source.getFile();
        if (!file.existsAsFile()) {
            result.setText("Choose a valid source audio file.");
            return;
        }
        auto* params = new juce::DynamicObject();
        params->setProperty("source", file.getFullPathName());
        params->setProperty("outputDir", output.getFile().getFullPathName());
        params->setProperty("mode", mode.getText());
        params->setProperty("engine", "demucs");
        params->setProperty("model", "htdemucs_ft");
        juce::Array<juce::var> stems;
        stems.add("vocals");
        stems.add("instrumental");
        params->setProperty("stems", juce::var(stems));
        job.track(worker.submit("Run AI processing", "mode.run", juce::var(params)));
    }

    StudioState& state;
    WorkerService& worker;
    StudioAudioEngine& audio;
    FilePickerRow source;
    FilePickerRow output;
    WaveformView waveform;
    JobStatusPanel job;
    JsonResultView result;
    juce::Label heading;
    juce::ComboBox mode;
    juce::TextButton run;
};

} // namespace

std::unique_ptr<juce::Component> createProcessingPanel(
    StudioState& state, WorkerService& worker, StudioAudioEngine& audio,
    juce::AudioFormatManager& formats) {
    return std::make_unique<ProcessingPanel>(state, worker, audio, formats);
}

} // namespace omnistem::desktop
