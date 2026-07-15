#include "FrequencyPaintWorkspace.h"
#include "StudioServices.h"

namespace omnistem::desktop {
namespace {
class FrequencyWorkflow final : public juce::Component {
public:
    explicit FrequencyWorkflow(WorkerService& service)
        : worker(service), source("Source audio", false, "*.wav;*.flac;*.aiff"), job(service) {
        heading.setText("Spectral Painting", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        output.setTextToShowWhenEmpty("Rendered WAV path", juce::Colours::grey);
        gain.setRange(-120.0, 24.0, 0.1);
        gain.setValue(-12.0);
        gain.setTextValueSuffix(" dB");
        clear.setButtonText("Clear Masks");
        render.setButtonText("Render Mask Chain");
        source.onFileChanged = [this](const juce::File& file) {
            if (file.existsAsFile()) canvas.loadFile(file);
        };
        gain.onValueChange = [this] { canvas.setGain(gain.getValue()); };
        clear.onClick = [this] { canvas.clearMasks(); };
        render.onClick = [this] { renderMasks(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            result.setValue(snapshot.payload);
            status.setText(snapshot.state == RemoteJobState::completed ? "Render completed" : snapshot.message,
                           juce::dontSendNotification);
        };
        addAndMakeVisible(heading);
        addAndMakeVisible(source);
        addAndMakeVisible(output);
        addAndMakeVisible(gain);
        addAndMakeVisible(clear);
        addAndMakeVisible(render);
        addAndMakeVisible(canvas);
        addAndMakeVisible(status);
        addAndMakeVisible(job);
        addAndMakeVisible(result);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        heading.setBounds(area.removeFromTop(40));
        source.setBounds(area.removeFromTop(38));
        output.setBounds(area.removeFromTop(36).reduced(2));
        auto row = area.removeFromTop(42);
        gain.setBounds(row.removeFromLeft(220).reduced(2));
        clear.setBounds(row.removeFromLeft(110).reduced(2));
        render.setBounds(row.removeFromLeft(160).reduced(2));
        canvas.setBounds(area.removeFromTop(360).reduced(0, 4));
        status.setBounds(area.removeFromTop(28));
        job.setBounds(area.removeFromTop(58));
        result.setBounds(area.reduced(0, 4));
    }

private:
    void renderMasks() {
        const auto input = source.getFile();
        const juce::File finalOutput(output.getText().trim());
        const auto& regions = canvas.masks();
        if (!input.existsAsFile() || finalOutput == juce::File{} || regions.empty()) {
            status.setText("Choose input/output and paint at least one region", juce::dontSendNotification);
            return;
        }
        const auto width = static_cast<double>(juce::jmax(1, canvas.getWidth()));
        const auto height = static_cast<double>(juce::jmax(1, canvas.getHeight()));
        const auto duration = canvas.durationSeconds();
        const auto nyquist = canvas.nyquistHz();
        auto* pipeline = new juce::DynamicObject();
        pipeline->setProperty("name", "Spectral mask chain");
        juce::Array<juce::var> steps;
        juce::String previous;
        for (std::size_t index = 0; index < regions.size(); ++index) {
            const auto id = "mask-" + juce::String(static_cast<int>(index + 1));
            const auto& box = regions[index].normalized;
            auto* params = new juce::DynamicObject();
            params->setProperty("source", previous.isEmpty()
                ? juce::var(input.getFullPathName())
                : juce::var("${" + previous + ".output}"));
            const auto destination = index + 1 == regions.size()
                ? finalOutput
                : finalOutput.getSiblingFile(finalOutput.getFileNameWithoutExtension()
                    + ".mask-" + juce::String(static_cast<int>(index + 1)) + ".wav");
            params->setProperty("output", destination.getFullPathName());
            params->setProperty("startSeconds", duration * box.getX() / width);
            params->setProperty("endSeconds", duration * box.getRight() / width);
            params->setProperty("lowHz", nyquist * (1.0 - box.getBottom() / height));
            params->setProperty("highHz", nyquist * (1.0 - box.getY() / height));
            params->setProperty("gainDb", regions[index].gainDb);
            auto* step = new juce::DynamicObject();
            step->setProperty("id", id);
            step->setProperty("method", "spectral.mask");
            step->setProperty("params", juce::var(params));
            juce::Array<juce::var> dependencies;
            if (previous.isNotEmpty()) dependencies.add(previous);
            step->setProperty("dependsOn", juce::var(dependencies));
            steps.add(juce::var(step));
            previous = id;
        }
        pipeline->setProperty("steps", juce::var(steps));
        auto* request = new juce::DynamicObject();
        request->setProperty("pipeline", juce::var(pipeline));
        job.track(worker.submit("Render spectral masks", "pipeline.run", juce::var(request)));
    }

    WorkerService& worker;
    FilePickerRow source;
    FrequencyPaintCanvas canvas;
    JobStatusPanel job;
    JsonResultView result;
    juce::Label heading, status;
    juce::TextEditor output;
    juce::Slider gain;
    juce::TextButton clear, render;
};
}

std::unique_ptr<juce::Component> createFrequencyPaintWorkspace(WorkerService& worker) {
    return std::make_unique<FrequencyWorkflow>(worker);
}
}
