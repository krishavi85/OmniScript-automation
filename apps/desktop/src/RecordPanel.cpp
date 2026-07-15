#include "StudioServices.h"

namespace omnistem::desktop {
namespace {
class RecordPanel final : public juce::Component,
                          private juce::Timer {
public:
    explicit RecordPanel(WorkerService& service) : worker(service) {
        title.setText("History", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        search.setTextToShowWhenEmpty("Search job history", juce::Colours::grey);
        records.setMultiLine(true);
        records.setReadOnly(true);
        search.onTextChange = [this] { refresh(); };
        addAndMakeVisible(title);
        addAndMakeVisible(search);
        addAndMakeVisible(records);
        startTimerHz(2);
    }

    ~RecordPanel() override { stopTimer(); }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(40));
        search.setBounds(area.removeFromTop(38).reduced(3));
        records.setBounds(area.reduced(0, 6));
    }

private:
    void timerCallback() override { refresh(); }
    void refresh() {
        const auto needle = search.getText().toLowerCase();
        juce::String text;
        for (const auto& item : worker.snapshots()) {
            const auto haystack = (item.title + " " + item.method + " " + item.message).toLowerCase();
            if (needle.isNotEmpty() && !haystack.contains(needle)) continue;
            text << item.startedAt.toString(true, true) << " | " << item.title
                 << " | " << toString(item.state) << " | "
                 << juce::String(item.progress * 100.0, 0) << "%\n";
            text << "  " << item.method << " — " << item.message << "\n\n";
        }
        records.setText(text, false);
    }

    WorkerService& worker;
    juce::Label title;
    juce::TextEditor search, records;
};
}

std::unique_ptr<juce::Component> createRecordPanel(WorkerService& worker) {
    return std::make_unique<RecordPanel>(worker);
}
}
