#include "StudioWidgets.h"
#include "ResultHooks.h"

namespace omnistem::desktop {

JobStatusPanel::JobStatusPanel(WorkerService& service) : worker(service) {
    worker.addChangeListener(this);
    stateLabel.setText("Idle", juce::dontSendNotification);
    messageLabel.setText("No active job", juce::dontSendNotification);
    messageLabel.setJustificationType(juce::Justification::centredLeft);
    cancelButton.setEnabled(false);
    cancelButton.onClick = [this] {
        if (localId.isNotEmpty())
            worker.cancel(localId);
    };
    addAndMakeVisible(progressBar);
    addAndMakeVisible(stateLabel);
    addAndMakeVisible(messageLabel);
    addAndMakeVisible(cancelButton);
}

JobStatusPanel::~JobStatusPanel() {
    worker.removeChangeListener(this);
}

void JobStatusPanel::track(const juce::String& localJobId) {
    localId = localJobId;
    notifiedTerminalId.clear();
    refresh();
}

void JobStatusPanel::clear() {
    localId.clear();
    notifiedTerminalId.clear();
    progress = 0.0;
    progressBar.repaint();
    stateLabel.setText("Idle", juce::dontSendNotification);
    messageLabel.setText("No active job", juce::dontSendNotification);
    cancelButton.setEnabled(false);
}

std::optional<RemoteJobSnapshot> JobStatusPanel::currentSnapshot() const {
    if (localId.isEmpty()) return std::nullopt;
    return worker.snapshot(localId);
}

void JobStatusPanel::resized() {
    auto area = getLocalBounds();
    stateLabel.setBounds(area.removeFromLeft(110).reduced(4, 2));
    cancelButton.setBounds(area.removeFromRight(85).reduced(4, 2));
    progressBar.setBounds(area.removeFromBottom(12).reduced(4, 1));
    messageLabel.setBounds(area.reduced(4, 2));
}

void JobStatusPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    refresh();
}

void JobStatusPanel::refresh() {
    const auto snapshot = currentSnapshot();
    if (!snapshot) {
        progress = 0.0;
        progressBar.repaint();
        stateLabel.setText("Idle", juce::dontSendNotification);
        messageLabel.setText("No active job", juce::dontSendNotification);
        cancelButton.setEnabled(false);
        return;
    }

    progress = snapshot->progress;
    progressBar.repaint();
    stateLabel.setText(toString(snapshot->state).toUpperCase(), juce::dontSendNotification);
    messageLabel.setText(snapshot->message, juce::dontSendNotification);
    const auto active = snapshot->state == RemoteJobState::queued
                     || snapshot->state == RemoteJobState::running;
    cancelButton.setEnabled(active);
    const auto terminal = snapshot->state == RemoteJobState::completed
                       || snapshot->state == RemoteJobState::failed
                       || snapshot->state == RemoteJobState::cancelled;
    if (terminal && notifiedTerminalId != snapshot->localId) {
        notifiedTerminalId = snapshot->localId;
        if (onTerminal) onTerminal(*snapshot);
    }
}

JsonResultView::JsonResultView() {
    editor.setMultiLine(true);
    editor.setReadOnly(true);
    editor.setScrollbarsShown(true);
    editor.setFont(juce::FontOptions(14.0f));
    editor.setColour(juce::TextEditor::backgroundColourId,
                     juce::Colour::fromRGB(15, 17, 23));
    addAndMakeVisible(editor);
}

void JsonResultView::setValue(const juce::var& value) {
    notifyResultHook(value);
    editor.setText(juce::JSON::toString(value, true), false);
}

void JsonResultView::setText(const juce::String& text) {
    editor.setText(text, false);
}

void JsonResultView::clear() {
    editor.clear();
}

void JsonResultView::resized() {
    editor.setBounds(getLocalBounds());
}

} // namespace omnistem::desktop
