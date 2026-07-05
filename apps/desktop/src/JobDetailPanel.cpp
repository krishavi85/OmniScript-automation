#include "StudioServices.h"

namespace omnistem::desktop {
namespace {
class JobDetailPanel final : public juce::Component,
                             private juce::Timer {
public:
    explicit JobDetailPanel(WorkerService& service) : worker(service) {
        title.setText("History Details", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        jobs.setTextWhenNothingSelected("Select a job");
        details.setMultiLine(true);
        details.setReadOnly(true);
        jobs.onChange = [this] { showSelected(); };
        addAndMakeVisible(title);
        addAndMakeVisible(jobs);
        addAndMakeVisible(details);
        startTimerHz(2);
        refresh();
    }

    ~JobDetailPanel() override { stopTimer(); }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(40));
        jobs.setBounds(area.removeFromTop(38).reduced(3));
        details.setBounds(area.reduced(0, 6));
    }

private:
    void timerCallback() override { refresh(); }

    void refresh() {
        const auto selected = jobs.getSelectedId();
        records = worker.snapshots();
        jobs.clear(juce::dontSendNotification);
        for (int index = 0; index < static_cast<int>(records.size()); ++index) {
            const auto& item = records[static_cast<std::size_t>(index)];
            jobs.addItem(item.title + " [" + toString(item.state) + "]", index + 1);
        }
        if (selected > 0 && selected <= jobs.getNumItems())
            jobs.setSelectedId(selected, juce::dontSendNotification);
        else if (!records.empty())
            jobs.setSelectedId(1, juce::dontSendNotification);
        showSelected();
    }

    void showSelected() {
        const auto index = jobs.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(records.size())) return;
        const auto& item = records[static_cast<std::size_t>(index)];
        juce::String text;
        text << item.title << "\nMethod: " << item.method
             << "\nState: " << toString(item.state)
             << "\nProgress: " << juce::String(item.progress * 100.0, 1) << "%"
             << "\nMessage: " << item.message
             << "\nStarted: " << item.startedAt.toString(true, true)
             << "\n\n" << juce::JSON::toString(item.payload, true);
        details.setText(text, false);
    }

    WorkerService& worker;
    std::vector<RemoteJobSnapshot> records;
    juce::Label title;
    juce::ComboBox jobs;
    juce::TextEditor details;
};
}

std::unique_ptr<juce::Component> createJobDetailPanel(WorkerService& worker) {
    return std::make_unique<JobDetailPanel>(worker);
}
}
