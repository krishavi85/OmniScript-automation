#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class GodPanel final : public juce::Component {
public:
    GodPanel(StudioState& stateToUse, WorkerService& workerToUse)
        : state(stateToUse), worker(workerToUse),
          source("Source audio", false, "*.wav;*.flac;*.aiff;*.mp3;*.ogg;*.m4a"),
          output("Output folder", true), job(workerToUse) {
        heading.setText("God Mode", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        source.setFile(state.getCurrentAudio());
        output.setFile(state.getOutputDirectory());
        engines.setText("demucs,openunmix,audio-separator");
        models.setText("htdemucs_ft,umxhq,UVR-MDX-NET-Inst_HQ_3.onnx");
        run.setButtonText("Run God Mode");
        validate.setButtonText("Validate");
        run.onClick = [this] { submit(false); };
        validate.onClick = [this] { submit(true); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            result.setValue(snapshot.payload);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(source);
        addAndMakeVisible(output);
        addAndMakeVisible(engines);
        addAndMakeVisible(models);
        addAndMakeVisible(run);
        addAndMakeVisible(validate);
        addAndMakeVisible(job);
        addAndMakeVisible(result);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        source.setBounds(area.removeFromTop(38));
        output.setBounds(area.removeFromTop(38));
        engines.setBounds(area.removeFromTop(34));
        models.setBounds(area.removeFromTop(34));
        auto row = area.removeFromTop(42);
        run.setBounds(row.removeFromLeft(140).reduced(3));
        validate.setBounds(row.removeFromLeft(110).reduced(3));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
        result.setBounds(area.reduced(0, 6));
    }

private:
    static juce::Array<juce::var> values(const juce::String& text) {
        juce::Array<juce::var> result;
        juce::StringArray tokens;
        tokens.addTokens(text, ",", "");
        for (auto token : tokens) {
            token = token.trim();
            if (token.isNotEmpty()) result.add(token);
        }
        return result;
    }

    void submit(bool dryRun) {
        if (!source.getFile().existsAsFile()) {
            result.setText("Choose a valid source file.");
            return;
        }
        auto* params = new juce::DynamicObject();
        params->setProperty("source", source.getFile().getFullPathName());
        params->setProperty("outputDir", output.getFile().getFullPathName());
        params->setProperty("mode", "god");
        params->setProperty("engines", juce::var(values(engines.getText())));
        params->setProperty("models", juce::var(values(models.getText())));
        juce::Array<juce::var> stems;
        stems.add("vocals");
        stems.add("instrumental");
        params->setProperty("stems", juce::var(stems));
        params->setProperty("quality", "maximum");
        job.track(worker.submit(dryRun ? "Validate God Mode" : "Run God Mode",
                                dryRun ? "mode.plan" : "mode.run",
                                juce::var(params)));
    }

    StudioState& state;
    WorkerService& worker;
    FilePickerRow source, output;
    JobStatusPanel job;
    JsonResultView result;
    juce::Label heading;
    juce::TextEditor engines, models;
    juce::TextButton run, validate;
};

} // namespace

std::unique_ptr<juce::Component> createGodPanel(StudioState& state,
                                                 WorkerService& worker) {
    return std::make_unique<GodPanel>(state, worker);
}

} // namespace omnistem::desktop
