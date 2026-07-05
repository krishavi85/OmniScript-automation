#include "StudioWidgets.h"

#include <algorithm>

namespace omnistem::desktop {

class StemMixerPanel::TrackRow final : public juce::Component {
public:
    TrackRow(StudioAudioEngine& engine,
             StudioAudioEngine::StemMixSnapshot initial)
        : audioEngine(engine), snapshot(std::move(initial)) {
        nameLabel.setText(snapshot.name, juce::dontSendNotification);
        nameLabel.setTooltip(snapshot.file.getFullPathName());
        gain.setRange(-60.0, 12.0, 0.1);
        gain.setValue(snapshot.gainDb, juce::dontSendNotification);
        gain.setTextValueSuffix(" dB");
        gain.setSliderStyle(juce::Slider::LinearHorizontal);
        pan.setRange(-1.0, 1.0, 0.01);
        pan.setValue(snapshot.pan, juce::dontSendNotification);
        pan.setSliderStyle(juce::Slider::LinearHorizontal);
        mute.setButtonText("M");
        solo.setButtonText("S");
        mute.setToggleState(snapshot.muted, juce::dontSendNotification);
        solo.setToggleState(snapshot.solo, juce::dontSendNotification);

        const auto update = [this] {
            audioEngine.setStemMix(snapshot.id,
                                   static_cast<float>(gain.getValue()),
                                   static_cast<float>(pan.getValue()),
                                   mute.getToggleState(),
                                   solo.getToggleState());
        };
        gain.onValueChange = update;
        pan.onValueChange = update;
        mute.onClick = update;
        solo.onClick = update;

        addAndMakeVisible(nameLabel);
        addAndMakeVisible(gain);
        addAndMakeVisible(pan);
        addAndMakeVisible(mute);
        addAndMakeVisible(solo);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(4, 2);
        nameLabel.setBounds(area.removeFromLeft(150));
        mute.setBounds(area.removeFromRight(36));
        solo.setBounds(area.removeFromRight(36));
        pan.setBounds(area.removeFromRight(170).reduced(4, 0));
        gain.setBounds(area.reduced(4, 0));
    }

private:
    StudioAudioEngine& audioEngine;
    StudioAudioEngine::StemMixSnapshot snapshot;
    juce::Label nameLabel;
    juce::Slider gain;
    juce::Slider pan;
    juce::ToggleButton mute;
    juce::ToggleButton solo;
};

StemMixerPanel::StemMixerPanel(StudioAudioEngine& engine) : audioEngine(engine) {
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    audioEngine.addChangeListener(this);
    refresh();
}

StemMixerPanel::~StemMixerPanel() {
    audioEngine.removeChangeListener(this);
    viewport.setViewedComponent(nullptr, false);
}

void StemMixerPanel::refresh() {
    const auto snapshots = audioEngine.stemMix();
    rows.clear();
    content.removeAllChildren();
    for (const auto& snapshot : snapshots) {
        auto row = std::make_unique<TrackRow>(audioEngine, snapshot);
        content.addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }
    content.setSize(std::max(600, viewport.getWidth() - 12),
                    std::max(1, static_cast<int>(rows.size()) * 48));
    resized();
}

void StemMixerPanel::resized() {
    viewport.setBounds(getLocalBounds());
    content.setSize(std::max(600, viewport.getWidth() - 12),
                    std::max(1, static_cast<int>(rows.size()) * 48));
    auto area = content.getLocalBounds();
    for (auto& row : rows)
        row->setBounds(area.removeFromTop(48));
}

void StemMixerPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (rows.size() != audioEngine.stemMix().size())
        refresh();
}

} // namespace omnistem::desktop
