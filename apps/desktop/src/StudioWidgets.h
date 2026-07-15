#pragma once

#include "StudioAudioEngine.h"
#include "StudioServices.h"

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

namespace omnistem::desktop {

class FilePickerRow final : public juce::Component,
                            private juce::Button::Listener {
public:
    FilePickerRow(juce::String labelText,
                  bool selectDirectory,
                  juce::String wildcard = "*");
    ~FilePickerRow() override;

    void setFile(const juce::File& file);
    juce::File getFile() const;
    void setEnabled(bool shouldBeEnabled);
    void resized() override;

    std::function<void(const juce::File&)> onFileChanged;

private:
    void buttonClicked(juce::Button*) override;

    juce::Label label;
    juce::TextEditor pathEditor;
    juce::TextButton browseButton{"Browse"};
    bool directoryMode{};
    juce::String filter;
    std::unique_ptr<juce::FileChooser> chooser;
};

class WaveformView final : public juce::Component,
                           private juce::ChangeListener {
public:
    explicit WaveformView(juce::AudioFormatManager& formats);
    ~WaveformView() override;

    void setFile(const juce::File& file);
    void clear();
    void setPlayhead(double normalizedPosition);
    void paint(juce::Graphics&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    juce::AudioThumbnailCache cache{8};
    juce::AudioThumbnail thumbnail;
    std::atomic<double> playhead{0.0};
};

class JobStatusPanel final : public juce::Component,
                             private juce::ChangeListener {
public:
    explicit JobStatusPanel(WorkerService& service);
    ~JobStatusPanel() override;

    void track(const juce::String& localJobId);
    void clear();
    juce::String trackedJob() const { return localId; }
    std::optional<RemoteJobSnapshot> currentSnapshot() const;
    void resized() override;

    std::function<void(const RemoteJobSnapshot&)> onTerminal;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();

    WorkerService& worker;
    juce::String localId;
    juce::String notifiedTerminalId;
    double progress{};
    juce::ProgressBar progressBar{progress};
    juce::Label stateLabel;
    juce::Label messageLabel;
    juce::TextButton cancelButton{"Cancel"};
};

class StemMixerPanel final : public juce::Component,
                             private juce::ChangeListener {
public:
    explicit StemMixerPanel(StudioAudioEngine& engine);
    ~StemMixerPanel() override;

    void refresh();
    void resized() override;

private:
    class TrackRow;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    StudioAudioEngine& audioEngine;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<TrackRow>> rows;
};

class JsonResultView final : public juce::Component {
public:
    JsonResultView();
    void setValue(const juce::var& value);
    void setText(const juce::String& text);
    void clear();
    void resized() override;

private:
    juce::TextEditor editor;
};

} // namespace omnistem::desktop
