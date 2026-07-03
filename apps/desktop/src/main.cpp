#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace omnistem {

struct PitchPoint {
    double timeSeconds{};
    double midiNote{};
};

struct NoteEvent {
    std::string id;
    std::string stemId;
    double startSeconds{};
    double durationSeconds{};
    double gainDb{};
    double pan{};
    double confidence{};
    bool muted{};
    std::vector<PitchPoint> pitchCurve;
};

class EditCommand {
public:
    virtual ~EditCommand() = default;
    virtual void apply(NoteEvent& note) = 0;
    virtual void undo(NoteEvent& note) = 0;
};

class SetGainCommand final : public EditCommand {
public:
    explicit SetGainCommand(double requestedGainDb)
        : requestedGainDb_(std::clamp(requestedGainDb, -120.0, 24.0)) {}

    void apply(NoteEvent& note) override {
        previousGainDb_ = note.gainDb;
        note.gainDb = requestedGainDb_;
    }

    void undo(NoteEvent& note) override {
        note.gainDb = previousGainDb_;
    }

private:
    double requestedGainDb_{};
    double previousGainDb_{};
};

} // namespace omnistem

int main() {
    using namespace omnistem;

    NoteEvent note{
        .id = "note-0001",
        .stemId = "lead-vocal",
        .startSeconds = 12.4,
        .durationSeconds = 0.82,
        .gainDb = 0.0,
        .pan = 0.0,
        .confidence = 0.93,
        .muted = false,
        .pitchCurve = {{0.0, 64.0}, {0.41, 64.18}, {0.82, 64.0}}
    };

    SetGainCommand lowerGain{-3.0};
    lowerGain.apply(note);

    std::cout << "OmniStem Desktop " << OMNISTEM_VERSION << '\n'
              << "Non-destructive note event: " << note.id << '\n'
              << "Stem: " << note.stemId << '\n'
              << "Gain after command: " << note.gainDb << " dB\n";

    lowerGain.undo(note);
    std::cout << "Gain after undo: " << note.gainDb << " dB\n";
    return 0;
}
