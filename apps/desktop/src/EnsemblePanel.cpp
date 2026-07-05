#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {
class EnsemblePanel final : public juce::Component {
public:
    explicit EnsemblePanel(WorkerService& service) : worker(service), job(service) {
        title.setText("Ensemble Builder", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        definition.setMultiLine(true);
        definition.setText(R"json({
  "source": "",
  "outputDir": "",
  "mode": "ensemble",
  "engines": ["demucs", "openunmix"],
  "models": ["htdemucs_ft", "umxhq"],
  "stems": ["vocals", "drums", "bass", "other"],
  "weights": [1, 1]
})json");
        validate.setButtonText("Validate");
        run.setButtonText("Run Ensemble");
        validate.onClick = [this] { submit("mode.plan", "Validate ensemble"); };
        run.onClick = [this] { submit("mode.run", "Run ensemble"); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            result.setValue(snapshot.payload);
            status.setText(snapshot.state == RemoteJobState::completed ? "Completed" : snapshot.message,
                           juce::dontSendNotification);
        };
        addAndMakeVisible(title);
        addAndMakeVisible(definition);
        addAndMakeVisible(validate);
        addAndMakeVisible(run);
        addAndMakeVisible(status);
        addAndMakeVisible(job);
        addAndMakeVisible(result);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(40));
        definition.setBounds(area.removeFromTop(220).reduced(0, 4));
        auto row = area.removeFromTop(42);
        validate.setBounds(row.removeFromLeft(120).reduced(2));
        run.setBounds(row.removeFromLeft(140).reduced(2));
        status.setBounds(area.removeFromTop(28));
        job.setBounds(area.removeFromTop(58));
        result.setBounds(area.reduced(0, 4));
    }

private:
    void submit(const juce::String& method, const juce::String& name) {
        const auto parsed = juce::JSON::parse(definition.getText());
        if (parsed.getDynamicObject() == nullptr) {
            status.setText("Definition must be a JSON object", juce::dontSendNotification);
            return;
        }
        job.track(worker.submit(name, method, parsed));
    }

    WorkerService& worker;
    JobStatusPanel job;
    JsonResultView result;
    juce::Label title, status;
    juce::TextEditor definition;
    juce::TextButton validate, run;
};
}

std::unique_ptr<juce::Component> createEnsemblePanel(WorkerService& worker) {
    return std::make_unique<EnsemblePanel>(worker);
}
}
