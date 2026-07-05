#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class ModelsWorkspace final : public juce::Component {
public:
    explicit ModelsWorkspace(WorkerService& service) : worker(service), job(service) {
        heading.setText("Models", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        modelId.setTextToShowWhenEmpty("Model ID", juce::Colours::grey);
        version.setTextToShowWhenEmpty("Version", juce::Colours::grey);
        sourceUrl.setTextToShowWhenEmpty("HTTPS artifact URL", juce::Colours::grey);
        sha256.setTextToShowWhenEmpty("SHA-256", juce::Colours::grey);
        licenseId.setTextToShowWhenEmpty("License ID", juce::Colours::grey);
        licenseUrl.setTextToShowWhenEmpty("License URL", juce::Colours::grey);
        accept.setButtonText("License reviewed and accepted");
        installed.setButtonText("Refresh Installed");
        install.setButtonText("Install or Update");
        remove.setButtonText("Remove");
        check.setButtonText("Check Version");

        installed.onClick = [this] { submit("Installed models", "model.installed.list", object()); };
        install.onClick = [this] { installSelected(); };
        remove.onClick = [this] { removeSelected(); };
        check.onClick = [this] { checkSelected(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            output.setValue(snapshot.payload);
            message.setText(snapshot.state == RemoteJobState::completed ? "Completed" : snapshot.message,
                            juce::dontSendNotification);
        };

        addAndMakeVisible(heading);
        addAndMakeVisible(modelId);
        addAndMakeVisible(version);
        addAndMakeVisible(sourceUrl);
        addAndMakeVisible(sha256);
        addAndMakeVisible(licenseId);
        addAndMakeVisible(licenseUrl);
        addAndMakeVisible(accept);
        addAndMakeVisible(installed);
        addAndMakeVisible(install);
        addAndMakeVisible(remove);
        addAndMakeVisible(check);
        addAndMakeVisible(message);
        addAndMakeVisible(job);
        addAndMakeVisible(output);
        installed.triggerClick();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        auto row = area.removeFromTop(36);
        modelId.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
        version.setBounds(row.reduced(2));
        sourceUrl.setBounds(area.removeFromTop(36).reduced(2));
        sha256.setBounds(area.removeFromTop(36).reduced(2));
        row = area.removeFromTop(36);
        licenseId.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
        licenseUrl.setBounds(row.reduced(2));
        accept.setBounds(area.removeFromTop(32));
        row = area.removeFromTop(42);
        installed.setBounds(row.removeFromLeft(140).reduced(2));
        install.setBounds(row.removeFromLeft(140).reduced(2));
        check.setBounds(row.removeFromLeft(120).reduced(2));
        remove.setBounds(row.removeFromLeft(100).reduced(2));
        message.setBounds(area.removeFromTop(28));
        job.setBounds(area.removeFromTop(58));
        output.setBounds(area.reduced(0, 4));
    }

private:
    static juce::var object() { return juce::var(new juce::DynamicObject()); }

    void submit(const juce::String& title, const juce::String& method, juce::var params) {
        job.track(worker.submit(title, method, std::move(params)));
    }

    bool validIdentity() {
        if (modelId.getText().trim().isEmpty() || version.getText().trim().isEmpty()) {
            message.setText("Model ID and version are required", juce::dontSendNotification);
            return false;
        }
        return true;
    }

    void installSelected() {
        if (!validIdentity()) return;
        if (!accept.getToggleState()) {
            message.setText("License acceptance is required", juce::dontSendNotification);
            return;
        }
        auto* value = new juce::DynamicObject();
        value->setProperty("modelId", modelId.getText().trim());
        value->setProperty("version", version.getText().trim());
        value->setProperty("sourceUrl", sourceUrl.getText().trim());
        value->setProperty("sha256", sha256.getText().trim().toLowerCase());
        value->setProperty("licenseId", licenseId.getText().trim());
        value->setProperty("licenseUrl", licenseUrl.getText().trim());
        value->setProperty("licenseAccepted", true);
        submit("Install model", "model.install", juce::var(value));
    }

    void removeSelected() {
        if (modelId.getText().trim().isEmpty()) return;
        auto* value = new juce::DynamicObject();
        value->setProperty("modelId", modelId.getText().trim());
        submit("Remove model", "model.remove", juce::var(value));
    }

    void checkSelected() {
        if (!validIdentity()) return;
        auto* value = new juce::DynamicObject();
        value->setProperty("modelId", modelId.getText().trim());
        value->setProperty("version", version.getText().trim());
        submit("Check model version", "model.updateStatus", juce::var(value));
    }

    WorkerService& worker;
    JobStatusPanel job;
    JsonResultView output;
    juce::Label heading, message;
    juce::TextEditor modelId, version, sourceUrl, sha256, licenseId, licenseUrl;
    juce::ToggleButton accept;
    juce::TextButton installed, install, remove, check;
};

} // namespace

std::unique_ptr<juce::Component> createModelsWorkspace(WorkerService& worker) {
    return std::make_unique<ModelsWorkspace>(worker);
}

} // namespace omnistem::desktop
