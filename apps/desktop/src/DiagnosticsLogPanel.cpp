#include "StudioServices.h"

namespace omnistem::desktop {
namespace {
class DiagnosticsLogPanel final : public juce::Component,
                                  private juce::ChangeListener {
public:
    explicit DiagnosticsLogPanel(LogStore& store) : logs(store) {
        title.setText("Logs", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        search.setTextToShowWhenEmpty("Search logs", juce::Colours::grey);
        level.addItemList({"All", "Info", "Warning", "Error"}, 1);
        level.setSelectedId(1);
        output.setMultiLine(true);
        output.setReadOnly(true);
        output.setScrollbarsShown(true);
        search.onTextChange = [this] { refresh(); };
        level.onChange = [this] { refresh(); };
        logs.addChangeListener(this);
        addAndMakeVisible(title);
        addAndMakeVisible(search);
        addAndMakeVisible(level);
        addAndMakeVisible(output);
        refresh();
    }

    ~DiagnosticsLogPanel() override { logs.removeChangeListener(this); }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(40));
        auto row = area.removeFromTop(38);
        level.setBounds(row.removeFromRight(140).reduced(3));
        search.setBounds(row.reduced(3));
        output.setBounds(area.reduced(0, 6));
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh() {
        const auto minimum = level.getText() == "All" ? juce::String{} : level.getText();
        juce::String text;
        for (const auto& item : logs.search(search.getText(), minimum))
            text << item.timestamp.toString(true, true) << " [" << item.level.toUpperCase()
                 << "] [" << item.category << "] " << item.message << "\n";
        output.setText(text, false);
    }

    LogStore& logs;
    juce::Label title;
    juce::TextEditor search, output;
    juce::ComboBox level;
};
}

std::unique_ptr<juce::Component> createDiagnosticsLogPanel(LogStore& logs) {
    return std::make_unique<DiagnosticsLogPanel>(logs);
}
}
