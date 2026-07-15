#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {

class ModelManagerPanel final : public juce::Component {
public:
    explicit ModelManagerPanel(WorkerService& service)
        : worker(service), job(service) {
        title.setText("Models Manager", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        query.setTextToShowWhenEmpty("Search catalog", juce::Colours::grey);
        modelId.setTextToShowWhenEmpty("Model ID", juce::Colours::grey);
        version.setTextToShowWhenEmpty("Version", juce::Colours::grey);
        sourceUrl.setTextToShowWhenEmpty("HTTPS artifact URL", juce::Colours::grey);
        checksum.setTextToShowWhenEmpty("SHA-256", juce::Colours::grey);
        licenseId.setTextToShowWhenEmpty("License ID", juce::Colours::grey);
        licenseUrl.setTextToShowWhenEmpty("License URL", juce::Colours::grey);
        accept.setButtonText("I have reviewed and accept this model license");
        search.setButtonText("Search Catalog");
        refresh.setButtonText("Installed Models");
        install.setButtonText("Install / Update");
        remove.setButtonText("Remove");
        status.setButtonText("Check Update");

        search.onClick = [this] { searchCatalog(); };
        refresh.onClick = [this] { run("Installed models", "model.installed.list", empty()); };
        install.onClick = [this] { installModel(); };
        remove.onClick = [this] { removeModel(); };
        status.onClick = [this] { checkUpdate(); };
        query.onReturnKey = [this] { searchCatalog(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
            output.setValue(snapshot.payload);
            message.setText(snapshot.state == RemoteJobState::completed
                                ? "Completed"
                                : snapshot.message,
                            juce::dontSendNotification);
        };

        for (juce::Component* item : {static_cast<juce::Component*>(&title), &query, &search,
                                      &refresh, &modelId, &version, &sourceUrl, &checksum,
                                      &licenseId, &licenseUrl, &accept, &install, &remove,
                                      &status, &message, &job, &output})
            addAndMakeVisible(item);
        refresh.triggerClick();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(40));
        auto row = area.removeFromTop(38);
        refresh.setBounds(row.removeFromRight(150).reduced(3));
        search.setBounds(row.removeFromRight(130).reduced(3));
        query.setBounds(row.reduced(3));
        row = area.removeFromTop(38);
        modelId.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(3));
        version.setBounds(row.reduced(3));
        sourceUrl.setBounds(area.removeFromTop(38).reduced(3));
        checksum.setBounds(area.removeFromTop(38).reduced(3));
        row = area.removeFromTop(38);
        licenseId.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(3));
        licenseUrl.setBounds(row.reduced(3));
        accept.setBounds(area.removeFromTop(34).reduced(3));
        row = area.removeFromTop(42);
        install.setBounds(row.removeFromLeft(140).reduced(3));
        status.setBounds(row.removeFromLeft(120).reduced(3));
        remove.setBounds(row.removeFromLeft(100).reduced(3));
        message.setBounds(area.removeFromTop(28));
        job.setBounds(area.removeFromTop(58).reduced(0, 4));
        output.setBounds(area.reduced(0, 4));
    }

private:
    static juce::var empty() { return juce::var(new juce::DynamicObject()); }

    void run(const juce::String& name, const juce::String& method, juce::var params) {
        job.track(worker.submit(name, method, std::move(params)));
    }

    bool requireIdentity() {
        if (modelId.getText().trim().isEmpty() || version.getText().trim().isEmpty()) {
            message.setText("Model ID and version are required", juce::dontSendNotification);
            return false;
        }
        return true;
    }

    void searchCatalog() {
        auto* params = new juce::DynamicObject();
        params->setProperty("query", query.getText().trim());
        params->setProperty("includeDeprecated", false);
        params->setProperty("sortBy", "name");
        run("Search model catalog", "catalog.models", juce::var(params));
    }

    void installModel() {
        if (!requireIdentity()) return;
        if (!accept.getToggleState()) {
            message.setText("Explicit license acceptance is required", juce::dontSendNotification);
            return;
        }
        auto* params = new juce::DynamicObject();
        params->setProperty("modelId", modelId.getText().trim());
        params->setProperty("version", version.getText().trim());
        params->setProperty("sourceUrl", sourceUrl.getText().trim());
        params->setProperty("sha256", checksum.getText().trim().toLowerCase());
        params->setProperty("licenseId", licenseId.getText().trim());
        params->setProperty("licenseUrl", licenseUrl.getText().trim());
        params->setProperty("licenseAccepted", true);
        run("Install model", "model.install", juce::var(params));
    }

    void removeModel() {
        if (modelId.getText().trim().isEmpty()) {
            message.setText("Model ID is required", juce::dontSendNotification);
            return;
        }
        auto* params = new juce::DynamicObject();
        params->setProperty("modelId", modelId.getText().trim());
        run("Remove model", "model.remove", juce::var(params));
    }

    void checkUpdate() {
        if (!requireIdentity()) return;
        auto* params = new juce::DynamicObject();
        params->setProperty("modelId", modelId.getText().trim());
        params->setProperty("version", version.getText().trim());
        run("Check model update", "model.updateStatus", juce::var(params));
    }

    WorkerService& worker;
    JobStatusPanel job;
    JsonResultView output;
    juce::Label title, message;
    juce::TextEditor query, modelId, version, sourceUrl, checksum, licenseId, licenseUrl;
    juce::TextButton search, refresh, install, remove, status;
    juce::ToggleButton accept;
};

} // namespace

std::unique_ptr<juce::Component> createModelManagerPanel(WorkerService& worker) {
    return std::make_unique<ModelManagerPanel>(worker);
}

} // namespace omnistem::desktop
