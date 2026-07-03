#include <JuceHeader.h>
#include "omnistem/core/OmniStemCore.h"

namespace {

class TimelineView final : public juce::Component,
                           private juce::ChangeListener {
public:
    explicit TimelineView(juce::AudioFormatManager& formatManager)
        : thumbnailCache(8), thumbnail(512, formatManager, thumbnailCache) {
        thumbnail.addChangeListener(this);
    }

    ~TimelineView() override {
        thumbnail.removeChangeListener(this);
    }

    void setFile(const juce::File& file) {
        thumbnail.setSource(new juce::FileInputSource(file));
    }

    void clear() {
        thumbnail.clear();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(18, 20, 27));
        g.setColour(juce::Colour::fromRGB(48, 54, 70));
        for (int x = 0; x < getWidth(); x += 80)
            g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));

        const auto waveformBounds = getLocalBounds().reduced(18).withTrimmedTop(26);
        if (thumbnail.getTotalLength() > 0.0) {
            g.setColour(juce::Colour::fromRGB(87, 202, 255));
            thumbnail.drawChannels(g, waveformBounds, 0.0, thumbnail.getTotalLength(), 1.0f);
        } else {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawFittedText("Import an audio file to display its waveform",
                             waveformBounds, juce::Justification::centred, 1);
        }

        g.setColour(juce::Colours::white);
        g.drawText("Waveform / stem timeline", getLocalBounds().reduced(12), juce::Justification::topLeft);
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    juce::AudioThumbnailCache thumbnailCache;
    juce::AudioThumbnail thumbnail;
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
        g.drawText("Spectral correction canvas — rendering is performed by the worker",
                   getLocalBounds().reduced(12), juce::Justification::topLeft);
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
    MainComponent()
        : deadMansPedalFile(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("OmniStemStudio")
                               .getChildFile("plugin-scan-dead-man.txt")),
          pluginList(pluginFormats, knownPlugins, deadMansPedalFile, nullptr, true),
          timeline(formatManager),
          tabs(juce::TabbedButtonBar::TabsAtTop) {
        formatManager.registerBasicFormats();
        pluginFormats.addDefaultFormats();
        deadMansPedalFile.getParentDirectory().createDirectory();
        pluginList.setNumberOfThreadsForScanning(2);
        pluginList.setOptionsButtonText("Scan / manage plugins");

        addAndMakeVisible(importButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(deviceButton);
        addAndMakeVisible(statusLabel);
        addAndMakeVisible(tabs);

        importButton.setButtonText("Import Audio");
        playButton.setButtonText("Play");
        stopButton.setButtonText("Stop");
        deviceButton.setButtonText("Audio Device");
        statusLabel.setText("Ready — WASAPI enabled; ASIO is an opt-in build option", juce::dontSendNotification);

        importButton.addListener(this);
        playButton.addListener(this);
        stopButton.addListener(this);
        deviceButton.addListener(this);
        transport.addChangeListener(this);

        tabs.addTab("Timeline", juce::Colour::fromRGB(30, 34, 44), &timeline, false);
        tabs.addTab("Spectral", juce::Colour::fromRGB(30, 34, 44), &spectral, false);
        tabs.addTab("Notes / MIDI", juce::Colour::fromRGB(30, 34, 44), &notes, false);
        tabs.addTab("Plugins", juce::Colour::fromRGB(30, 34, 44), &pluginList, false);

        setAudioChannels(0, 2);
        setSize(1280, 800);
    }

    ~MainComponent() override {
        transport.stop();
        transport.setSource(nullptr);
        readerSource.reset();
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
        deviceButton.setBounds(controls.removeFromLeft(120).reduced(2));
        statusLabel.setBounds(controls.reduced(8, 2));
        tabs.setBounds(area.reduced(0, 8));
    }

private:
    void buttonClicked(juce::Button* button) override {
        if (button == &importButton) {
            chooser = std::make_unique<juce::FileChooser>(
                "Import WAV, AIFF, FLAC, MP3 or OGG", juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& selected) {
                    if (safeThis != nullptr) safeThis->loadFile(selected.getResult());
                });
        } else if (button == &playButton) {
            if (readerSource) transport.start();
        } else if (button == &stopButton) {
            transport.stop();
            transport.setPosition(0.0);
        } else if (button == &deviceButton) {
            showAudioDeviceDialog();
        }
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        statusLabel.setText(transport.isPlaying() ? "Playing" : "Stopped", juce::dontSendNotification);
    }

    void showAudioDeviceDialog() {
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 0, 1, 2, true, true, true, false);
        selector->setSize(520, 430);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector.release());
        options.dialogTitle = "OmniStem Audio Device";
        options.dialogBackgroundColour = juce::Colour::fromRGB(25, 28, 36);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    }

    void loadFile(const juce::File& file) {
        if (!file.existsAsFile()) return;

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (!reader) {
            statusLabel.setText("Unsupported or unreadable audio file", juce::dontSendNotification);
            return;
        }

        const auto sampleRate = reader->sampleRate;
        auto nextSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

        transport.stop();
        transport.setSource(nullptr);
        readerSource.reset();
        readerSource = std::move(nextSource);
        transport.setSource(readerSource.get(), 0, nullptr, sampleRate);
        timeline.setFile(file);
        statusLabel.setText("Loaded: " + file.getFileName(), juce::dontSendNotification);
    }

    juce::TextButton importButton, playButton, stopButton, deviceButton;
    juce::Label statusLabel;
    juce::AudioFormatManager formatManager;
    juce::AudioPluginFormatManager pluginFormats;
    juce::KnownPluginList knownPlugins;
    juce::File deadMansPedalFile;
    juce::PluginListComponent pluginList;
    TimelineView timeline;
    SpectralView spectral;
    NoteEditorView notes;
    juce::TabbedComponent tabs;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser> chooser;
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

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
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
