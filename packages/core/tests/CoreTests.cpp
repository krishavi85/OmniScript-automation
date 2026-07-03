#include "omnistem/core/OmniStemCore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    using namespace omnistem;
    const auto root = std::filesystem::temp_directory_path() / "omnistem-core-tests";
    std::filesystem::create_directories(root);
    const auto db = root / "projects.sqlite";
    const auto midi = root / "notes.mid";
    std::filesystem::remove(db);
    std::filesystem::remove(midi);

    Project first;
    first.id = "project-a";
    first.name = "First";
    first.stems.push_back({"vocals", "Vocals", StemRole::leadVocal, "vocals.wav", -1.0, 0.2, false, true});
    first.notes.push_back({"note-1", "vocals", 0.25, 0.75, -2.0, -0.1, 1.5, 0.94, false,
                           {{0.0, 64.0}, {0.75, 64.25}}, {{0.0, 1.0}, {0.75, 0.4}}});
    first.masks.push_back({"mask-1", "other", "vocals", 0.0, 1.0, 100.0, 3000.0, -9.0, 0.25});
    first.processingGraph = ProcessingGraphFactory::masteringChain();

    ProjectRepository repository(db);
    std::string error;
    expect(repository.open(error), "SQLite repository opens");
    expect(repository.save(first, error), "First project saves");

    Project second = first;
    second.id = "project-b";
    second.name = "Second";
    expect(repository.save(second, error), "Different projects may reuse local stem/note/mask IDs");

    const auto loaded = repository.load(first.id, error);
    expect(loaded.has_value(), "Saved project loads");
    if (loaded) {
        expect(loaded->stems.size() == 1, "Stem count round-trips");
        expect(loaded->stems[0].role == StemRole::leadVocal, "Stem role round-trips");
        expect(loaded->notes.size() == 1 && loaded->notes[0].gainEnvelope.size() == 2,
               "Gain envelope round-trips");
        expect(loaded->masks.size() == 1, "Spectral masks round-trip");
        expect(loaded->processingGraph.size() == first.processingGraph.size(),
               "Processing graph round-trips");
        expect(loaded->processingGraph.back().parameters.contains("ceiling_db"),
               "Processor parameters round-trip");
    }

    SetNoteGainCommand command("note-1", -12.0);
    command.apply(first);
    expect(first.notes[0].gainDb == -12.0, "Edit command applies");
    command.undo(first);
    expect(first.notes[0].gainDb == -2.0, "Edit command undoes");

    expect(MidiWriter::writeType1(midi, first.notes, 120.0, 480, error), "MIDI writes");
    std::ifstream midiInput(midi, std::ios::binary);
    char header[4]{};
    midiInput.read(header, 4);
    expect(std::string(header, 4) == "MThd", "MIDI header is valid");

    JobManager jobs(1);
    expect(jobs.submit({"job", "test", "progress"},
        [](const std::atomic_bool&, const JobManager::ProgressCallback& progress) {
            progress(0.5, "half");
        }), "Job submits");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto snapshot = jobs.snapshot("job");
    expect(snapshot && snapshot->state == JobState::completed && snapshot->progress == 1.0,
           "Job completes");
    jobs.shutdown();

    const auto request = AiRequestBuilder::assistantPlan("quote: \" and slash: \\");
    expect(request.find("\\\"") != std::string::npos && request.find("\\\\") != std::string::npos,
           "JSON request escapes input");

    if (failures == 0) std::cout << "All OmniStem core tests passed\n";
    return failures == 0 ? 0 : 1;
}
