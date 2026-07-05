#include <JuceHeader.h>
#include "StudioAudioEngine.h"
#include "StudioServices.h"
#include "StudioWidgets.h"

namespace omnistem::desktop {
std::unique_ptr<juce::Component> makeDashboardWorkspace(StudioState&, WorkerService&, LogStore&);
std::unique_ptr<juce::Component> createProcessingPanel(StudioState&, WorkerService&,
                                                       StudioAudioEngine&, juce::AudioFormatManager&);
std::unique_ptr<juce::Component> createBatchPanel(StudioState&, WorkerService&);

namespace {
class Shell final : public juce::AudioAppComponent {
public:
    Shell() : state(logs), worker(state, logs), audio(formats),
              tabs(juce::TabbedButtonBar::TabsAtTop) {
        formats.registerBasicFormats();
        addAndMakeVisible(tabs);
        addTab("Dashboard", makeDashboardWorkspace(state, worker, logs));
        addTab("Separate", createProcessingPanel(state, worker, audio, formats));
        addTab("Batch", createBatchPanel(state, worker));
        setAudioChannels(0, 2);
        setSize(1440, 880);
    }

    ~Shell() override {
        audio.stop();
        shutdownAudio();
        tabs.clearTabs();
        pages.clear();
    }

    void prepareToPlay(int block, double rate) override { audio.prepareToPlay(block, rate); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        audio.getNextAudioBlock(info);
    }
    void releaseResources() override { audio.releaseResources(); }
    void resized() override { tabs.setBounds(getLocalBounds().reduced(8)); }

private:
    void addTab(const juce::String& name, std::unique_ptr<juce::Component> page) {
        auto* raw = page.get();
        pages.push_back(std::move(page));
        tabs.addTab(name, juce::Colour::fromRGB(28, 31, 40), raw, false);
    }
    LogStore logs;
    StudioState state;
    WorkerService worker;
    juce::AudioFormatManager formats;
    StudioAudioEngine audio;
    juce::TabbedComponent tabs;
    std::vector<std::unique_ptr<juce::Component>> pages;
};

class ShellWindow final : public juce::DocumentWindow {
public:
    ShellWindow() : DocumentWindow("OmniStem Studio",
        juce::Colour::fromRGB(14, 16, 22), allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new Shell(), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class ShellApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "OmniStem Studio"; }
    const juce::String getApplicationVersion() override { return OMNISTEM_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { window = std::make_unique<ShellWindow>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<ShellWindow> window;
};
}
}

START_JUCE_APPLICATION(omnistem::desktop::ShellApplication)
