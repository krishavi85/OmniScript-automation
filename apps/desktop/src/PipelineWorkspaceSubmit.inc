void PipelineWorkspace::submit(const juce::String& method, const juce::String& title) {
    const auto pipeline = juce::JSON::parse(definition.getText());
    if (pipeline.getDynamicObject() == nullptr) {
        status.setText("Definition must be valid JSON", juce::dontSendNotification);
        return;
    }
    auto* request = new juce::DynamicObject();
    request->setProperty("pipeline", pipeline);
    if (method == "pipeline.validate") {
        job.track(worker.submit(title, "pipeline.validate", juce::var(request)));
        return;
    }
    if (method == "pipeline.run") {
        job.track(worker.submit(title, "pipeline.run", juce::var(request)));
        return;
    }
    delete request;
}
