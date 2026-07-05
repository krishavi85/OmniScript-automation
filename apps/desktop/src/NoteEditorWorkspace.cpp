#include "StudioServices.h"
#include "StudioWidgets.h"

#include <cmath>

namespace omnistem::desktop {
namespace {
struct EditableNote {
    double start{};
    double end{};
    double midi{60.0};
    double velocity{1.0};
};

class NoteEditorWorkspace final : public juce::Component {
public:
    explicit NoteEditorWorkspace(WorkerService& service)
        : worker(service), source("Source stem", false, "*.wav;*.flac;*.aiff"),
          output("Rendered audio", false, "*.wav"), job(service) {
        heading.setText("Note Object Editor", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        transcribe.setButtonText("Transcribe");
        render.setButtonText("Render Selected Note");
        gain.setRange(-120.0, 24.0, 0.1);
        gain.setValue(-12.0);
        pitchShift.setRange(-24.0, 24.0, 0.1);
        pitchShift.setValue(0.0);
        notes.onChange = [this] { showSelected(); };
        transcribe.onClick = [this] { beginTranscription(); };
        render.onClick = [this] { renderSelected(); };
        job.onTerminal = [this](const RemoteJobSnapshot& snapshot) { completed(snapshot); };
        addAndMakeVisible(heading);
        addAndMakeVisible(source);
        addAndMakeVisible(output);
        addAndMakeVisible(transcribe);
        addAndMakeVisible(notes);
        addAndMakeVisible(start);
        addAndMakeVisible(end);
        addAndMakeVisible(midi);
        addAndMakeVisible(gain);
        addAndMakeVisible(pitchShift);
        addAndMakeVisible(render);
        addAndMakeVisible(message);
        addAndMakeVisible(job);
        addAndMakeVisible(result);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        heading.setBounds(area.removeFromTop(40));
        source.setBounds(area.removeFromTop(38));
        output.setBounds(area.removeFromTop(38));
        auto row = area.removeFromTop(40);
        transcribe.setBounds(row.removeFromLeft(120).reduced(2));
        notes.setBounds(row.reduced(2));
        row = area.removeFromTop(40);
        start.setBounds(row.removeFromLeft(130).reduced(2));
        end.setBounds(row.removeFromLeft(130).reduced(2));
        midi.setBounds(row.removeFromLeft(110).reduced(2));
        gain.setBounds(row.removeFromLeft(180).reduced(2));
        pitchShift.setBounds(row.reduced(2));
        render.setBounds(area.removeFromTop(40).removeFromLeft(180).reduced(2));
        message.setBounds(area.removeFromTop(28));
        job.setBounds(area.removeFromTop(58));
        result.setBounds(area.reduced(0, 4));
    }

private:
    void beginTranscription() {
        if (!source.getFile().existsAsFile()) return;
        auto* value = new juce::DynamicObject();
        value->setProperty("source", source.getFile().getFullPathName());
        job.track(worker.submit("Transcribe notes", "transcription.run", juce::var(value)));
        waitingForTranscription = true;
    }

    void completed(const RemoteJobSnapshot& snapshot) {
        result.setValue(snapshot.payload);
        if (snapshot.state != RemoteJobState::completed) {
            message.setText(snapshot.message, juce::dontSendNotification);
            return;
        }
        if (waitingForTranscription) {
            waitingForTranscription = false;
            loadTranscription(snapshot.payload);
        }
    }

    void loadTranscription(const juce::var& payload) {
        auto* root = payload.getDynamicObject();
        auto* files = root != nullptr ? root->getProperty("noteEventFiles").getArray() : nullptr;
        if (files == nullptr || files->isEmpty()) {
            message.setText("Transcription returned no note-event CSV", juce::dontSendNotification);
            return;
        }
        const juce::File csv(files->getFirst().toString());
        juce::StringArray lines;
        lines.addLines(csv.loadFileAsString());
        noteObjects.clear();
        notes.clear(juce::dontSendNotification);
        for (int lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
            juce::StringArray columns;
            columns.addTokens(lines[lineIndex], ",", "\"");
            if (columns.size() < 3) continue;
            EditableNote note;
            note.start = columns[0].getDoubleValue();
            note.end = columns[1].getDoubleValue();
            note.midi = columns[2].getDoubleValue();
            if (columns.size() > 3) note.velocity = columns[3].getDoubleValue();
            noteObjects.push_back(note);
            notes.addItem("MIDI " + juce::String(note.midi, 1) + "  "
                          + juce::String(note.start, 3) + "–" + juce::String(note.end, 3),
                          static_cast<int>(noteObjects.size()));
        }
        if (!noteObjects.empty()) notes.setSelectedId(1, juce::sendNotification);
        message.setText("Loaded " + juce::String(noteObjects.size()) + " note objects",
                        juce::dontSendNotification);
    }

    void showSelected() {
        const auto index = notes.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(index, static_cast<int>(noteObjects.size()))) return;
        const auto& note = noteObjects[static_cast<std::size_t>(index)];
        start.setText(juce::String(note.start, 6));
        end.setText(juce::String(note.end, 6));
        midi.setText(juce::String(note.midi, 3));
    }

    void renderSelected() {
        if (!source.getFile().existsAsFile() || output.getFile() == juce::File{}) return;
        const auto midiValue = midi.getText().getDoubleValue();
        auto* value = new juce::DynamicObject();
        value->setProperty("source", source.getFile().getFullPathName());
        value->setProperty("output", output.getFile().getFullPathName());
        value->setProperty("startSeconds", start.getText().getDoubleValue());
        value->setProperty("endSeconds", end.getText().getDoubleValue());
        value->setProperty("fundamentalHz", 440.0 * std::pow(2.0, (midiValue - 69.0) / 12.0));
        value->setProperty("gainDb", gain.getValue());
        value->setProperty("pitchShiftSemitones", pitchShift.getValue());
        job.track(worker.submit("Resynthesize note", "note.resynthesize", juce::var(value)));
    }

    WorkerService& worker;
    FilePickerRow source, output;
    JobStatusPanel job;
    JsonResultView result;
    juce::Label heading, message;
    juce::ComboBox notes;
    juce::TextEditor start, end, midi;
    juce::Slider gain, pitchShift;
    juce::TextButton transcribe, render;
    std::vector<EditableNote> noteObjects;
    bool waitingForTranscription{};
};
}

std::unique_ptr<juce::Component> createNoteEditorWorkspace(WorkerService& worker) {
    return std::make_unique<NoteEditorWorkspace>(worker);
}
}
