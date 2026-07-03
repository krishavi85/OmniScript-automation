#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace omnistem {

enum class StemRole { leadVocal, backingVocal, drums, bass, guitar, piano, strings, synth, effects, other };
enum class JobState { queued, running, completed, failed, cancelled };
enum class ProcessorKind { restoration, mastering, replacement, plugin };

struct PitchPoint {
    double timeSeconds{};
    double midiNote{};
};

struct EnvelopePoint {
    double timeSeconds{};
    double value{};
};

struct NoteEvent {
    std::string id;
    std::string stemId;
    double startSeconds{};
    double durationSeconds{};
    double gainDb{};
    double pan{};
    double formantSemitones{};
    double confidence{};
    bool muted{};
    std::vector<PitchPoint> pitchCurve;
    std::vector<EnvelopePoint> gainEnvelope;
};

struct Stem {
    std::string id;
    std::string name;
    StemRole role{StemRole::other};
    std::filesystem::path audioPath;
    double gainDb{};
    double pan{};
    bool muted{};
    bool solo{};
};

struct SpectralMaskOperation {
    std::string id;
    std::string sourceStemId;
    std::string destinationStemId;
    double startSeconds{};
    double endSeconds{};
    double lowFrequencyHz{};
    double highFrequencyHz{};
    double gainDb{};
    double feather{};
};

struct ProcessorNode {
    std::string id;
    ProcessorKind kind{ProcessorKind::plugin};
    std::string name;
    bool enabled{true};
    std::map<std::string, double> parameters;
};

struct Project {
    std::string id;
    std::string name;
    double sampleRate{48000.0};
    std::vector<Stem> stems;
    std::vector<NoteEvent> notes;
    std::vector<SpectralMaskOperation> masks;
    std::vector<ProcessorNode> processingGraph;
};

class EditCommand {
public:
    virtual ~EditCommand() = default;
    virtual void apply(Project& project) = 0;
    virtual void undo(Project& project) = 0;
};

class SetNoteGainCommand final : public EditCommand {
public:
    SetNoteGainCommand(std::string noteId, double gainDb);
    void apply(Project& project) override;
    void undo(Project& project) override;

private:
    std::string noteId_;
    double requestedGainDb_{};
    double previousGainDb_{};
    bool applied_{};
};

class ProjectRepository {
public:
    explicit ProjectRepository(std::filesystem::path databasePath);
    ~ProjectRepository();
    ProjectRepository(const ProjectRepository&) = delete;
    ProjectRepository& operator=(const ProjectRepository&) = delete;

    bool open(std::string& error);
    bool save(const Project& project, std::string& error);
    std::optional<Project> load(const std::string& projectId, std::string& error);
    bool isAvailable() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct JobSpec {
    std::string id;
    std::string type;
    std::string description;
};

struct JobSnapshot {
    JobSpec spec;
    JobState state{JobState::queued};
    double progress{};
    std::string message;
};

class JobManager {
public:
    using ProgressCallback = std::function<void(double, std::string)>;
    using Work = std::function<void(const std::atomic_bool&, const ProgressCallback&)>;

    explicit JobManager(std::size_t workerCount = 2);
    ~JobManager();
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    bool submit(JobSpec spec, Work work);
    bool cancel(const std::string& id);
    std::optional<JobSnapshot> snapshot(const std::string& id) const;
    std::vector<JobSnapshot> snapshots() const;
    void shutdown();

private:
    struct JobControl;
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::shared_ptr<JobControl>> queue_;
    std::map<std::string, std::shared_ptr<JobControl>> jobs_;
    std::vector<std::thread> workers_;
    bool stopping_{};
};

class MidiWriter {
public:
    static bool writeType1(const std::filesystem::path& path,
                           const std::vector<NoteEvent>& notes,
                           double tempoBpm,
                           std::uint16_t ticksPerQuarter,
                           std::string& error);
};

class ProcessingGraphFactory {
public:
    static std::vector<ProcessorNode> restorationChain();
    static std::vector<ProcessorNode> masteringChain();
    static ProcessorNode replacementNode(std::string instrumentName);
};

class AiRequestBuilder {
public:
    static std::string separation(const std::filesystem::path& source,
                                  const std::string& quality,
                                  const std::vector<std::string>& stems);
    static std::string transcription(const std::string& stemId, bool includePitchBends);
    static std::string assistantPlan(const std::string& instruction);
};

std::string toString(StemRole role);
std::string toString(JobState state);

} // namespace omnistem
