#include "StudioServices.h"

namespace omnistem::desktop {
namespace {

class HistoryView final : public juce::Component,
                          private juce::Timer {
public:
    explicit HistoryView(WorkerService& workerToUse) : worker(workerToUse) {
        heading.setText("Job History", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        search.setTextToShowWhenEmpty("Search jobs", juce::Colours::grey);
        output.setMultiLine(true);
        output.setReadOnly(true);
        output.setScrollbarsShown(true);
        search.onTextChange = [this] { refresh(); };
        addAndMakeVisible(heading);
        addAndMakeVisible(search);
        addAndMakeVisible(output);
        startTimerHz(2);
        refresh();
    }

    ~HistoryView() override { stopTimer(); }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        heading.setBounds(area.removeFromTop(40));
        search.setBounds(area.removeFromTop(38).reduced(3));
        output.setBounds(area.reduced(0, 6));
    }

private:
    void timerCallback() override { refresh(); }

    void refresh() {
        const auto needle = search.getText().toLowerCase();
        juce::String text;
        for (const auto& item : worker.snapshots()) {
            const auto searchable = (item.title + " " + item.method + " " + item.message).toLowerCase();
            if (needle.isNotEmpty() && !searchable.contains(needle)) continue;
            text << item.startedAt.toString(true, true) << " | " << item.title
                 << " | " << toString(item.state)
                 << " | " << juce::String(item.progress * 100.0, 0) << "%\n";
            text << "  " << item.method << " — " << item.message << "\n";
            if (!item.payload.isVoid())
                text << "  " << juce::JSON::toString(item.payload, false) << "\n";
            text << "\n";
        }
        output.setText(text, false);
    }

    WorkerService& worker;
    juce::Label heading;
    juce::TextEditor search;
    juce::TextEditor output;
};

} // namespace

std::unique_ptr<juce::Component> createHistoryView(WorkerService& worker) {
    return std::make_unique<HistoryView>(worker);
}

} // namespace omnistem::desktop
