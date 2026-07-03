#include <JuceHeader.h>
#include "omnistem/core/OmniStemCore.h"

namespace {

class TimelineView final : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(18, 20, 27));
        g.setColour(juce::Colour::fromRGB(48, 54, 70));
        for (int x = 0; x < getWidth(); x += 80) g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
        g.setColour(juce::Colour::fromRGB(87, 202, 255));
        g.fillRoundedRectangle(50.0f, 40.0f, static_cast<float>(getWidth() - 100), 54.0f, 8.0f);
        g.setColour(juce::Colours::white);
        g.drawText("Waveform / stem lanes", getLocalBounds().reduced(12), juce::Justification::topLeft);
    }
};

class SpectralView final : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        juce::ColourGradient gradient(juce::Colour::fromRGB(8, 10, 18), 0.0f, static_cast<float>(getHeight()),
                                      juce::Colour::fromRGB(80, 34, 128), static_cast<float>(getWidth()), 0.0f, false);
        gradient.addColour(0.55, juce::Colour::fromRGB(15, 112, 145));
        g.setGradientFill(gradient);
        g.fillAll();
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawText("Spectral correction canvas", getLocalBounds().reduced(12), juce::Justification::topLeft);
    }
};

class NoteEditorView final : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(22, 24, 31));
        for (int row = 0; row < 12; ++row) {
            g.setColour((row % 2 == 0 ? juce::Colours::white : juce::Colours::grey).withAlpha(0.06f));
            g.fillRect(0, row * 24, getWidth(), 24);
        }
        g.setColour(juce::Colour::fromRGB(121, 235, 155));
        g.fillRoundedRectangle(80.0f, 96.0f, 190.0f, 20.0f, 5.0f);
        g.fillRoundedRectangle(300.0f, 72.0f, 130.0f, 20.0f, 5.0f);
        g.setColour(juce::Colours::white);
        g.drawText("Non-destructive note objects / MIDI", getLocalBounds().reduced(12), juce::Justification::topLeft);
    }
};

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Button::Listener,
                            private juce::ChangeListener {
public:
    MainComponent() : tabs(juce::TabbedButtonBar::TabsAtTop) {
        formatManager.registerBasicFormats();
        pluginFormats.addDefaultFormats();

        addAndMakeVisible(importButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(statusLabel);
        addAndMakeVisible(tabs);

        importButton.setButtonText("Import Audio");
        playButton.setButtonText("Play");
        stopButton.setButtonText("Stop");
        statusLabel.setText("Ready — ASIO/WASAPI device engine", juce::dontSendNotification);

        importButton.addListener(this);
        playButton.addListener(this);
        stopButton.addListener(this);
        transport.addChangeListener(this);

        tabs.addTab("Timeline", juce::Colour::fromRGB(30, 34, 44), &timeline, false);
        tabs.addTab("Spectral", juce::Colour::fromRGB(30, 34, 44), &spectral, false);
        tabs.addTab("Notes / MIDI", juce::Colour::fromRGB(30, 34, 44), &notes, false);

        setAudioChannels(0, 2);
        setSize(1280, 800);
    }

    ~MainComponent() override {
        transport.setSource(nullptr);
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override {
        transport.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override {
        transport.getNextAudioBlock(bufferToFill);
    }

    void releaseResources() override { transport.releaseResources(); }

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        auto controls = area.removeFromTop(38);
        importButton.setBounds(controls.removeFromLeft(130).reduced(2));
        playButton.setBounds(controls.removeFromLeft(80).reduced(2));
        stopButton.setBounds(controls.removeFromLeft(80).reduced(2));
        statusLabel.setBounds(controls.reduced(8, 2));
        tabs.setBounds(area.reduced(0, 8));
    }

private:
    void buttonClicked(juce::Button* button) override {
        if (button == &importButton) {
            chooser = std::make_unique<juce::FileChooser>("Import WAV, AIFF, FLAC, MP3 or OGG", juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& selected) { loadFile(selected.getResult()); });
        } else if (button == &playButton) {
            transport.start();
        } else if (button == &stopButton) {
            transport.stop();
            transport.setPosition(0.0);
        }
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        statusLabel.setText(transport.isPlaying() ? "Playing" : "Stopped", juce::dontSendNotification);
    }

    void loadFile(const juce::File& file) {
        if (!file.existsAsFile()) return;
        if (auto* reader = formatManager.createReaderFor(file)) {
            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
            transport.setSource(readerSource.get(), 0, nullptr, reader->sampleRate);
            statusLabel.setText("Loaded: " + file.getFileName(), juce::dontSendNotification);
        } else {
            statusLabel.setText("Unsupported or unreadable audio file", juce::dontSendNotification);
        }
    }

    juce::TextButton importButton, playButton, stopButton;
    juce::Label statusLabel;
    juce::TabbedComponent tabs;
    TimelineView timeline;
    SpectralView spectral;
    NoteEditorView notes;
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::AudioPluginFormatManager pluginFormats;
    juce::KnownPluginList knownPlugins;
};

class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow() : DocumentWindow("OmniStem Studio", juce::Colour::fromRGB(14, 16, 22), allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class OmniStemApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "OmniStem Studio"; }
    const juce::String getApplicationVersion() override { return OMNISTEM_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> window;
};

} // namespace

START_JUCE_APPLICATION(OmniStemApplication)
