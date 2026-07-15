#include "StudioWidgets.h"

namespace omnistem::desktop {

FilePickerRow::FilePickerRow(juce::String labelText,
                             bool selectDirectory,
                             juce::String wildcard)
    : directoryMode(selectDirectory), filter(std::move(wildcard)) {
    label.setText(std::move(labelText), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    pathEditor.setTextToShowWhenEmpty(directoryMode ? "Select a folder" : "Select a file",
                                      juce::Colours::grey);
    browseButton.addListener(this);
    addAndMakeVisible(label);
    addAndMakeVisible(pathEditor);
    addAndMakeVisible(browseButton);

    pathEditor.onFocusLost = [this] {
        if (onFileChanged)
            onFileChanged(getFile());
    };
    pathEditor.onReturnKey = [this] {
        if (onFileChanged)
            onFileChanged(getFile());
    };
}

FilePickerRow::~FilePickerRow() {
    browseButton.removeListener(this);
}

void FilePickerRow::setFile(const juce::File& file) {
    pathEditor.setText(file.getFullPathName(), juce::dontSendNotification);
}

juce::File FilePickerRow::getFile() const {
    return juce::File(pathEditor.getText().trim());
}

void FilePickerRow::setEnabled(bool shouldBeEnabled) {
    Component::setEnabled(shouldBeEnabled);
    pathEditor.setEnabled(shouldBeEnabled);
    browseButton.setEnabled(shouldBeEnabled);
}

void FilePickerRow::resized() {
    auto area = getLocalBounds();
    label.setBounds(area.removeFromLeft(130));
    browseButton.setBounds(area.removeFromRight(90).reduced(4, 2));
    pathEditor.setBounds(area.reduced(4, 2));
}

void FilePickerRow::buttonClicked(juce::Button*) {
    const auto initial = getFile();
    chooser = std::make_unique<juce::FileChooser>(
        directoryMode ? "Choose folder" : "Choose audio file",
        initial.exists() ? initial : juce::File{}, filter);
    const auto flags = directoryMode
        ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
        : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags,
        [safeThis = juce::Component::SafePointer<FilePickerRow>(this)](const juce::FileChooser& selected) {
            if (safeThis == nullptr) return;
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            safeThis->setFile(file);
            if (safeThis->onFileChanged)
                safeThis->onFileChanged(file);
        });
}

WaveformView::WaveformView(juce::AudioFormatManager& formats)
    : thumbnail(512, formats, cache) {
    thumbnail.addChangeListener(this);
}

WaveformView::~WaveformView() {
    thumbnail.removeChangeListener(this);
}

void WaveformView::setFile(const juce::File& file) {
    if (file.existsAsFile())
        thumbnail.setSource(new juce::FileInputSource(file));
}

void WaveformView::clear() {
    thumbnail.clear();
    repaint();
}

void WaveformView::setPlayhead(double normalizedPosition) {
    playhead.store(juce::jlimit(0.0, 1.0, normalizedPosition));
    repaint();
}

void WaveformView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 27));
    const auto bounds = getLocalBounds().reduced(8);
    g.setColour(juce::Colour::fromRGB(52, 58, 75));
    for (int x = bounds.getX(); x < bounds.getRight(); x += 80)
        g.drawVerticalLine(x, static_cast<float>(bounds.getY()),
                          static_cast<float>(bounds.getBottom()));

    if (thumbnail.getTotalLength() > 0.0) {
        g.setColour(juce::Colour::fromRGB(75, 198, 255));
        thumbnail.drawChannels(g, bounds, 0.0, thumbnail.getTotalLength(), 1.0f);
        const auto x = bounds.getX() + static_cast<int>(playhead.load() * bounds.getWidth());
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawVerticalLine(x, static_cast<float>(bounds.getY()),
                          static_cast<float>(bounds.getBottom()));
    } else {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawFittedText("No audio loaded", bounds, juce::Justification::centred, 1);
    }
}

void WaveformView::changeListenerCallback(juce::ChangeBroadcaster*) {
    repaint();
}

} // namespace omnistem::desktop
