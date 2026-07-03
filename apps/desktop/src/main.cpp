#include "omnistem/core/OmniStemCore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int main() {
    using namespace omnistem;

    Project project;
    project.id = "demo-project";
    project.name = "OmniStem Demo";
    project.stems.push_back({"lead-vocal", "Lead Vocal", StemRole::leadVocal, "lead-vocal.wav"});
    project.notes.push_back({
        .id = "note-0001",
        .stemId = "lead-vocal",
        .startSeconds = 0.25,
        .durationSeconds = 0.75,
        .gainDb = 0.0,
        .pan = 0.0,
        .formantSemitones = 0.0,
        .confidence = 0.93,
        .muted = false,
        .pitchCurve = {{0.0, 64.0}, {0.35, 64.18}, {0.75, 64.0}},
        .gainEnvelope = {},
    });
    project.masks.push_back({"mask-1", "other", "lead-vocal", 1.0, 1.5, 250.0, 3500.0, -12.0, 0.2});
    project.processingGraph = ProcessingGraphFactory::restorationChain();
    const auto mastering = ProcessingGraphFactory::masteringChain();
    project.processingGraph.insert(project.processingGraph.end(), mastering.begin(), mastering.end());

    SetNoteGainCommand lowerGain{"note-0001", -3.0};
    lowerGain.apply(project);
    lowerGain.undo(project);

    std::string error;
    const auto midiPath = std::filesystem::current_path() / "omnistem-demo.mid";
    const bool midiWritten = MidiWriter::writeType1(midiPath, project.notes, 120.0, 480, error);

    ProjectRepository repository(std::filesystem::current_path() / "omnistem-demo.sqlite");
    const bool stored = repository.isAvailable() && repository.open(error) && repository.save(project, error);

    JobManager jobs{1};
    jobs.submit({"job-demo", "analysis", "Build waveform and spectrogram caches"},
        [](const std::atomic_bool& cancelled, const JobManager::ProgressCallback& progress) {
            for (int step = 1; step <= 5 && !cancelled.load(); ++step) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                progress(step / 5.0, "analysis tile " + std::to_string(step));
            }
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto job = jobs.snapshot("job-demo");

    std::cout << "OmniStem Studio core " << OMNISTEM_VERSION << '\n'
              << "Stems: " << project.stems.size() << ", notes: " << project.notes.size() << '\n'
              << "MIDI: " << (midiWritten ? midiPath.string() : error) << '\n'
              << "Persistence: " << (stored ? "SQLite project saved" : "SQLite optional/not available") << '\n'
              << "Job: " << (job ? toString(job->state) : "missing") << '\n'
              << "AI request: " << AiRequestBuilder::separation("song.wav", "ensemble", {"vocals", "drums", "bass", "other"}) << '\n';

    jobs.shutdown();
    return midiWritten ? 0 : 1;
}
