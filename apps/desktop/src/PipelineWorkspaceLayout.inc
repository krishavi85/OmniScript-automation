PipelineWorkspace::PipelineWorkspace(WorkerService& service)
    : worker(service), job(service) {
    heading.setText("Pipeline Builder", juce::dontSendNotification);
    heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    palette.addItemList({"mode.run", "transcription.run", "restoration.run",
                         "mastering.run", "spectral.mask", "instrument.render"}, 1);
    palette.setSelectedId(1);
    addStep.setButtonText("Add Step");
    reset.setButtonText("Reset");
    validate.setButtonText("Validate");
    run.setButtonText("Run");
    definition.setMultiLine(true);
    resetDefinition();
    addStep.onClick = [this] { appendStep(); };
    reset.onClick = [this] { resetDefinition(); };
    validate.onClick = [this] { submit("pipeline.validate", "Validate pipeline"); };
    run.onClick = [this] { submit("pipeline.run", "Run pipeline"); };
    job.onTerminal = [this](const RemoteJobSnapshot& snapshot) {
        result.setValue(snapshot.payload);
        status.setText(snapshot.state == RemoteJobState::completed ? "Completed" : snapshot.message,
                       juce::dontSendNotification);
    };
    addAndMakeVisible(heading);
    addAndMakeVisible(palette);
    addAndMakeVisible(addStep);
    addAndMakeVisible(reset);
    addAndMakeVisible(validate);
    addAndMakeVisible(run);
    addAndMakeVisible(definition);
    addAndMakeVisible(status);
    addAndMakeVisible(job);
    addAndMakeVisible(result);
}

void PipelineWorkspace::resized() {
    auto area = getLocalBounds().reduced(12);
    heading.setBounds(area.removeFromTop(40));
    auto controls = area.removeFromTop(38);
    palette.setBounds(controls.removeFromLeft(230).reduced(2));
    addStep.setBounds(controls.removeFromLeft(100).reduced(2));
    reset.setBounds(controls.removeFromLeft(80).reduced(2));
    validate.setBounds(controls.removeFromLeft(100).reduced(2));
    run.setBounds(controls.removeFromLeft(80).reduced(2));
    definition.setBounds(area.removeFromTop(300).reduced(0, 4));
    status.setBounds(area.removeFromTop(28));
    job.setBounds(area.removeFromTop(58));
    result.setBounds(area.reduced(0, 4));
}
