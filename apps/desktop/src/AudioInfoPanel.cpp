#include "StudioAudioEngine.h"
#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
namespace {
class AudioInfoPanel final : public juce::Component {
public:
    AudioInfoPanel(StudioState& stateToUse,
                   StudioAudioEngine& audioToUse,
                   juce::AudioFormatManager& formatsToUse)
        : state(stateToUse), audio(audioToUse), formats(formatsToUse),
          source("Audio file", false, "*.wav;*.flac;*.aiff;*.mp3;*.ogg;*.m4a"),
          waveform(formatsToUse) {
        heading.setText("Audio Inspector", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        source.onFileChanged = [this](const juce::File& file) { load(file); };
        play.setButtonText("Play");
        stop.setButtonText("Stop");
        play.onClick = [this] { audio.play(); };
        stop.onClick = [this] { audio.stop(); };
        details.setMultiLine(true);
        details.setReadOnly(true);
        addAndMakeVisible(heading);
        addAndMakeVisible(source);
        addAndMakeVisible(play);
        addAndMakeVisible(stop);
        addAndMakeVisible(waveform);
        addAndMakeVisible(details);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        source.setBounds(area.removeFromTop(38));
        auto row = area.removeFromTop(42);
        play.setBounds(row.removeFromLeft(90).reduced(3));
        stop.setBounds(row.removeFromLeft(90).reduced(3));
        waveform.setBounds(area.removeFromTop(190).reduced(0, 4));
        details.setBounds(area.reduced(0, 6));
    }

private:
    void load(const juce::File& file) {
        if (!file.existsAsFile()) return;
        state.setCurrentAudio(file);
        waveform.setFile(file);
        juce::String error;
        audio.loadPreview(file, error);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader) {
            details.setText(error, false);
            return;
        }
        juce::String text;
        text << "File: " << file.getFullPathName() << "\n";
        text << "Format: " << reader->getFormatName() << "\n";
        text << "Sample rate: " << reader->sampleRate << " Hz\n";
        text << "Channels: " << static_cast<int>(reader->numChannels) << "\n";
        text << "Bits: " << reader->bitsPerSample << "\n";
        text << "Duration: " << juce::String(reader->lengthInSamples / reader->sampleRate, 3) << " seconds\n";
        details.setText(text, false);
    }

    StudioState& state;
    StudioAudioEngine& audio;
    juce::AudioFormatManager& formats;
    FilePickerRow source;
    WaveformView waveform;
    juce::Label heading;
    juce::TextButton play, stop;
    juce::TextEditor details;
};
}

std::unique_ptr<juce::Component> createAudioInfoPanel(
    StudioState& state, StudioAudioEngine& audio, juce::AudioFormatManager& formats) {
    return std::make_unique<AudioInfoPanel>(state, audio, formats);
}
}
