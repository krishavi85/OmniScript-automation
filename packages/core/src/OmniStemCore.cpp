#include "omnistem/core/OmniStemCore.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#if OMNISTEM_HAS_SQLITE
#include <sqlite3.h>
#endif

namespace omnistem {
namespace {

NoteEvent* findNote(Project& project, const std::string& id) {
    const auto it = std::find_if(project.notes.begin(), project.notes.end(), [&](const NoteEvent& note) {
        return note.id == id;
    });
    return it == project.notes.end() ? nullptr : &*it;
}

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (const char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

std::string serializePitch(const std::vector<PitchPoint>& points) {
    std::ostringstream out;
    out << std::setprecision(15);
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i != 0) out << ';';
        out << points[i].timeSeconds << ',' << points[i].midiNote;
    }
    return out.str();
}

std::vector<PitchPoint> parsePitch(const std::string& text) {
    std::vector<PitchPoint> result;
    std::istringstream input(text);
    std::string token;
    while (std::getline(input, token, ';')) {
        const auto comma = token.find(',');
        if (comma == std::string::npos) continue;
        try {
            result.push_back({std::stod(token.substr(0, comma)), std::stod(token.substr(comma + 1))});
        } catch (...) {
        }
    }
    return result;
}

void appendBe16(std::vector<std::uint8_t>& data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    data.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendBe32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    data.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendVarLen(std::vector<std::uint8_t>& data, std::uint32_t value) {
    std::uint32_t buffer = value & 0x7f;
    while ((value >>= 7) != 0) {
        buffer <<= 8;
        buffer |= ((value & 0x7f) | 0x80);
    }
    for (;;) {
        data.push_back(static_cast<std::uint8_t>(buffer & 0xff));
        if ((buffer & 0x80) == 0) break;
        buffer >>= 8;
    }
}

#if OMNISTEM_HAS_SQLITE
bool execSql(sqlite3* db, const char* sql, std::string& error) {
    char* rawError = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &rawError) != SQLITE_OK) {
        error = rawError ? rawError : "SQLite operation failed";
        sqlite3_free(rawError);
        return false;
    }
    return true;
}

bool bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}
#endif

} // namespace

SetNoteGainCommand::SetNoteGainCommand(std::string noteId, double gainDb)
    : noteId_(std::move(noteId)), requestedGainDb_(std::clamp(gainDb, -120.0, 24.0)) {}

void SetNoteGainCommand::apply(Project& project) {
    if (auto* note = findNote(project, noteId_)) {
        previousGainDb_ = note->gainDb;
        note->gainDb = requestedGainDb_;
        applied_ = true;
    }
}

void SetNoteGainCommand::undo(Project& project) {
    if (!applied_) return;
    if (auto* note = findNote(project, noteId_)) note->gainDb = previousGainDb_;
    applied_ = false;
}

struct ProjectRepository::Impl {
    explicit Impl(std::filesystem::path path) : path(std::move(path)) {}
    std::filesystem::path path;
#if OMNISTEM_HAS_SQLITE
    sqlite3* db{};
#endif
};

ProjectRepository::ProjectRepository(std::filesystem::path databasePath)
    : impl_(std::make_unique<Impl>(std::move(databasePath))) {}

ProjectRepository::~ProjectRepository() {
#if OMNISTEM_HAS_SQLITE
    if (impl_->db) sqlite3_close(impl_->db);
#endif
}

bool ProjectRepository::open(std::string& error) {
#if OMNISTEM_HAS_SQLITE
    if (impl_->db) return true;
    if (sqlite3_open(impl_->path.string().c_str(), &impl_->db) != SQLITE_OK) {
        error = sqlite3_errmsg(impl_->db);
        return false;
    }
    constexpr const char* schema = R"SQL(
        PRAGMA foreign_keys=ON;
        CREATE TABLE IF NOT EXISTS projects(
            id TEXT PRIMARY KEY, name TEXT NOT NULL, sample_rate REAL NOT NULL
        );
        CREATE TABLE IF NOT EXISTS stems(
            id TEXT PRIMARY KEY, project_id TEXT NOT NULL, name TEXT NOT NULL,
            role TEXT NOT NULL, audio_path TEXT NOT NULL, gain_db REAL NOT NULL,
            pan REAL NOT NULL, muted INTEGER NOT NULL, solo INTEGER NOT NULL,
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS notes(
            id TEXT PRIMARY KEY, project_id TEXT NOT NULL, stem_id TEXT NOT NULL,
            start_seconds REAL NOT NULL, duration_seconds REAL NOT NULL,
            gain_db REAL NOT NULL, pan REAL NOT NULL, formant REAL NOT NULL,
            confidence REAL NOT NULL, muted INTEGER NOT NULL, pitch_curve TEXT NOT NULL,
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS spectral_masks(
            id TEXT PRIMARY KEY, project_id TEXT NOT NULL, source_stem_id TEXT NOT NULL,
            destination_stem_id TEXT NOT NULL, start_seconds REAL NOT NULL,
            end_seconds REAL NOT NULL, low_hz REAL NOT NULL, high_hz REAL NOT NULL,
            gain_db REAL NOT NULL, feather REAL NOT NULL,
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
    )SQL";
    return execSql(impl_->db, schema, error);
#else
    error = "SQLite3 was not found when OmniStemCore was built";
    return false;
#endif
}

bool ProjectRepository::save(const Project& project, std::string& error) {
#if OMNISTEM_HAS_SQLITE
    if (!impl_->db && !open(error)) return false;
    if (!execSql(impl_->db, "BEGIN IMMEDIATE;", error)) return false;
    auto rollback = [&] { std::string ignored; execSql(impl_->db, "ROLLBACK;", ignored); };

    sqlite3_stmt* statement{};
    const char* upsertProject = "INSERT INTO projects(id,name,sample_rate) VALUES(?,?,?) "
                                "ON CONFLICT(id) DO UPDATE SET name=excluded.name,sample_rate=excluded.sample_rate;";
    if (sqlite3_prepare_v2(impl_->db, upsertProject, -1, &statement, nullptr) != SQLITE_OK ||
        !bindText(statement, 1, project.id) || !bindText(statement, 2, project.name) ||
        sqlite3_bind_double(statement, 3, project.sampleRate) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        error = sqlite3_errmsg(impl_->db); sqlite3_finalize(statement); rollback(); return false;
    }
    sqlite3_finalize(statement);

    for (const char* table : {"stems", "notes", "spectral_masks"}) {
        const std::string sql = std::string("DELETE FROM ") + table + " WHERE project_id=?;";
        if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK ||
            !bindText(statement, 1, project.id) || sqlite3_step(statement) != SQLITE_DONE) {
            error = sqlite3_errmsg(impl_->db); sqlite3_finalize(statement); rollback(); return false;
        }
        sqlite3_finalize(statement);
    }

    const char* insertStem = "INSERT INTO stems VALUES(?,?,?,?,?,?,?,?,?);";
    for (const auto& stem : project.stems) {
        if (sqlite3_prepare_v2(impl_->db, insertStem, -1, &statement, nullptr) != SQLITE_OK) { rollback(); return false; }
        bindText(statement, 1, stem.id); bindText(statement, 2, project.id); bindText(statement, 3, stem.name);
        bindText(statement, 4, toString(stem.role)); bindText(statement, 5, stem.audioPath.string());
        sqlite3_bind_double(statement, 6, stem.gainDb); sqlite3_bind_double(statement, 7, stem.pan);
        sqlite3_bind_int(statement, 8, stem.muted ? 1 : 0); sqlite3_bind_int(statement, 9, stem.solo ? 1 : 0);
        if (sqlite3_step(statement) != SQLITE_DONE) { error = sqlite3_errmsg(impl_->db); sqlite3_finalize(statement); rollback(); return false; }
        sqlite3_finalize(statement);
    }

    const char* insertNote = "INSERT INTO notes VALUES(?,?,?,?,?,?,?,?,?,?,?);";
    for (const auto& note : project.notes) {
        if (sqlite3_prepare_v2(impl_->db, insertNote, -1, &statement, nullptr) != SQLITE_OK) { rollback(); return false; }
        bindText(statement, 1, note.id); bindText(statement, 2, project.id); bindText(statement, 3, note.stemId);
        sqlite3_bind_double(statement, 4, note.startSeconds); sqlite3_bind_double(statement, 5, note.durationSeconds);
        sqlite3_bind_double(statement, 6, note.gainDb); sqlite3_bind_double(statement, 7, note.pan);
        sqlite3_bind_double(statement, 8, note.formantSemitones); sqlite3_bind_double(statement, 9, note.confidence);
        sqlite3_bind_int(statement, 10, note.muted ? 1 : 0); bindText(statement, 11, serializePitch(note.pitchCurve));
        if (sqlite3_step(statement) != SQLITE_DONE) { error = sqlite3_errmsg(impl_->db); sqlite3_finalize(statement); rollback(); return false; }
        sqlite3_finalize(statement);
    }

    const char* insertMask = "INSERT INTO spectral_masks VALUES(?,?,?,?,?,?,?,?,?,?);";
    for (const auto& mask : project.masks) {
        if (sqlite3_prepare_v2(impl_->db, insertMask, -1, &statement, nullptr) != SQLITE_OK) { rollback(); return false; }
        bindText(statement, 1, mask.id); bindText(statement, 2, project.id); bindText(statement, 3, mask.sourceStemId);
        bindText(statement, 4, mask.destinationStemId); sqlite3_bind_double(statement, 5, mask.startSeconds);
        sqlite3_bind_double(statement, 6, mask.endSeconds); sqlite3_bind_double(statement, 7, mask.lowFrequencyHz);
        sqlite3_bind_double(statement, 8, mask.highFrequencyHz); sqlite3_bind_double(statement, 9, mask.gainDb);
        sqlite3_bind_double(statement, 10, mask.feather);
        if (sqlite3_step(statement) != SQLITE_DONE) { error = sqlite3_errmsg(impl_->db); sqlite3_finalize(statement); rollback(); return false; }
        sqlite3_finalize(statement);
    }

    return execSql(impl_->db, "COMMIT;", error);
#else
    (void)project;
    error = "SQLite3 was not found when OmniStemCore was built";
    return false;
#endif
}

std::optional<Project> ProjectRepository::load(const std::string& projectId, std::string& error) {
#if OMNISTEM_HAS_SQLITE
    if (!impl_->db && !open(error)) return std::nullopt;
    Project project;
    sqlite3_stmt* statement{};
    if (sqlite3_prepare_v2(impl_->db, "SELECT id,name,sample_rate FROM projects WHERE id=?;", -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(impl_->db); return std::nullopt;
    }
    bindText(statement, 1, projectId);
    if (sqlite3_step(statement) != SQLITE_ROW) { sqlite3_finalize(statement); error = "Project not found"; return std::nullopt; }
    project.id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    project.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    project.sampleRate = sqlite3_column_double(statement, 2);
    sqlite3_finalize(statement);

    if (sqlite3_prepare_v2(impl_->db, "SELECT id,name,role,audio_path,gain_db,pan,muted,solo FROM stems WHERE project_id=?;", -1, &statement, nullptr) == SQLITE_OK) {
        bindText(statement, 1, projectId);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            Stem stem;
            stem.id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            stem.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            stem.audioPath = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
            stem.gainDb = sqlite3_column_double(statement, 4); stem.pan = sqlite3_column_double(statement, 5);
            stem.muted = sqlite3_column_int(statement, 6) != 0; stem.solo = sqlite3_column_int(statement, 7) != 0;
            project.stems.push_back(std::move(stem));
        }
        sqlite3_finalize(statement);
    }

    if (sqlite3_prepare_v2(impl_->db, "SELECT id,stem_id,start_seconds,duration_seconds,gain_db,pan,formant,confidence,muted,pitch_curve FROM notes WHERE project_id=?;", -1, &statement, nullptr) == SQLITE_OK) {
        bindText(statement, 1, projectId);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            NoteEvent note;
            note.id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            note.stemId = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            note.startSeconds = sqlite3_column_double(statement, 2); note.durationSeconds = sqlite3_column_double(statement, 3);
            note.gainDb = sqlite3_column_double(statement, 4); note.pan = sqlite3_column_double(statement, 5);
            note.formantSemitones = sqlite3_column_double(statement, 6); note.confidence = sqlite3_column_double(statement, 7);
            note.muted = sqlite3_column_int(statement, 8) != 0;
            note.pitchCurve = parsePitch(reinterpret_cast<const char*>(sqlite3_column_text(statement, 9)));
            project.notes.push_back(std::move(note));
        }
        sqlite3_finalize(statement);
    }
    return project;
#else
    (void)projectId;
    error = "SQLite3 was not found when OmniStemCore was built";
    return std::nullopt;
#endif
}

bool ProjectRepository::isAvailable() const noexcept {
#if OMNISTEM_HAS_SQLITE
    return true;
#else
    return false;
#endif
}

struct JobManager::JobControl {
    JobSnapshot snapshot;
    Work work;
    std::atomic_bool cancelRequested{false};
};

JobManager::JobManager(std::size_t workerCount) {
    workerCount = std::max<std::size_t>(1, workerCount);
    workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) workers_.emplace_back([this] { workerLoop(); });
}

JobManager::~JobManager() { shutdown(); }

bool JobManager::submit(JobSpec spec, Work work) {
    std::lock_guard lock(mutex_);
    if (stopping_ || jobs_.contains(spec.id)) return false;
    auto control = std::make_shared<JobControl>();
    control->snapshot.spec = std::move(spec);
    control->work = std::move(work);
    jobs_.emplace(control->snapshot.spec.id, control);
    queue_.push(control);
    condition_.notify_one();
    return true;
}

bool JobManager::cancel(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    it->second->cancelRequested.store(true);
    if (it->second->snapshot.state == JobState::queued) it->second->snapshot.state = JobState::cancelled;
    return true;
}

std::optional<JobSnapshot> JobManager::snapshot(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto it = jobs_.find(id);
    return it == jobs_.end() ? std::nullopt : std::optional<JobSnapshot>(it->second->snapshot);
}

std::vector<JobSnapshot> JobManager::snapshots() const {
    std::lock_guard lock(mutex_);
    std::vector<JobSnapshot> result;
    result.reserve(jobs_.size());
    for (const auto& [_, job] : jobs_) result.push_back(job->snapshot);
    return result;
}

void JobManager::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        for (auto& [_, job] : jobs_) job->cancelRequested.store(true);
    }
    condition_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    workers_.clear();
}

void JobManager::workerLoop() {
    for (;;) {
        std::shared_ptr<JobControl> control;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            control = queue_.front(); queue_.pop();
            if (control->cancelRequested.load()) continue;
            control->snapshot.state = JobState::running;
        }
        try {
            control->work(control->cancelRequested, [this, control](double progress, std::string message) {
                std::lock_guard lock(mutex_);
                control->snapshot.progress = std::clamp(progress, 0.0, 1.0);
                control->snapshot.message = std::move(message);
            });
            std::lock_guard lock(mutex_);
            control->snapshot.state = control->cancelRequested.load() ? JobState::cancelled : JobState::completed;
            if (control->snapshot.state == JobState::completed) control->snapshot.progress = 1.0;
        } catch (const std::exception& exception) {
            std::lock_guard lock(mutex_);
            control->snapshot.state = JobState::failed;
            control->snapshot.message = exception.what();
        } catch (...) {
            std::lock_guard lock(mutex_);
            control->snapshot.state = JobState::failed;
            control->snapshot.message = "Unknown job failure";
        }
    }
}

bool MidiWriter::writeType1(const std::filesystem::path& path,
                            const std::vector<NoteEvent>& notes,
                            double tempoBpm,
                            std::uint16_t ticksPerQuarter,
                            std::string& error) {
    if (!(tempoBpm > 0.0) || ticksPerQuarter == 0) { error = "Invalid MIDI timing settings"; return false; }
    struct Event { std::uint32_t tick; bool on; std::uint8_t note; std::uint8_t velocity; };
    std::vector<Event> events;
    const double ticksPerSecond = static_cast<double>(ticksPerQuarter) * tempoBpm / 60.0;
    for (const auto& note : notes) {
        if (note.muted || note.durationSeconds <= 0.0 || note.pitchCurve.empty()) continue;
        const auto midi = static_cast<int>(std::lround(note.pitchCurve.front().midiNote));
        const auto clampedNote = static_cast<std::uint8_t>(std::clamp(midi, 0, 127));
        const auto velocity = static_cast<std::uint8_t>(std::clamp(100 + static_cast<int>(note.gainDb * 2.0), 1, 127));
        const auto start = static_cast<std::uint32_t>(std::max(0.0, note.startSeconds * ticksPerSecond));
        const auto end = static_cast<std::uint32_t>(std::max<double>(start + 1, (note.startSeconds + note.durationSeconds) * ticksPerSecond));
        events.push_back({start, true, clampedNote, velocity});
        events.push_back({end, false, clampedNote, 0});
    }
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on);
    });

    std::vector<std::uint8_t> track;
    const auto microsPerQuarter = static_cast<std::uint32_t>(std::lround(60000000.0 / tempoBpm));
    appendVarLen(track, 0); track.insert(track.end(), {0xff, 0x51, 0x03,
        static_cast<std::uint8_t>((microsPerQuarter >> 16) & 0xff),
        static_cast<std::uint8_t>((microsPerQuarter >> 8) & 0xff),
        static_cast<std::uint8_t>(microsPerQuarter & 0xff)});
    std::uint32_t previousTick{};
    for (const auto& event : events) {
        appendVarLen(track, event.tick - previousTick);
        track.push_back(event.on ? 0x90 : 0x80); track.push_back(event.note); track.push_back(event.velocity);
        previousTick = event.tick;
    }
    appendVarLen(track, 0); track.insert(track.end(), {0xff, 0x2f, 0x00});

    std::vector<std::uint8_t> file;
    file.insert(file.end(), {'M','T','h','d'}); appendBe32(file, 6); appendBe16(file, 1); appendBe16(file, 1); appendBe16(file, ticksPerQuarter);
    file.insert(file.end(), {'M','T','r','k'}); appendBe32(file, static_cast<std::uint32_t>(track.size())); file.insert(file.end(), track.begin(), track.end());
    std::ofstream output(path, std::ios::binary);
    if (!output) { error = "Cannot open MIDI output file"; return false; }
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!output) { error = "Failed while writing MIDI output"; return false; }
    return true;
}

std::vector<ProcessorNode> ProcessingGraphFactory::restorationChain() {
    return {
        {"restore-dc", ProcessorKind::restoration, "DC Removal", true, {{"amount", 1.0}}},
        {"restore-denoise", ProcessorKind::restoration, "Adaptive Denoise", true, {{"strength", 0.35}}},
        {"restore-declick", ProcessorKind::restoration, "Transient Declick", true, {{"sensitivity", 0.5}}},
        {"restore-dereverb", ProcessorKind::restoration, "Dereverb", false, {{"amount", 0.25}}},
    };
}

std::vector<ProcessorNode> ProcessingGraphFactory::masteringChain() {
    return {
        {"master-eq", ProcessorKind::mastering, "Corrective EQ", true, {{"tilt_db", 0.0}}},
        {"master-comp", ProcessorKind::mastering, "Multiband Dynamics", true, {{"amount", 0.25}}},
        {"master-width", ProcessorKind::mastering, "Stereo Width", true, {{"width", 1.0}}},
        {"master-limit", ProcessorKind::mastering, "True-Peak Limiter", true, {{"ceiling_db", -1.0}}},
    };
}

ProcessorNode ProcessingGraphFactory::replacementNode(std::string instrumentName) {
    return {"replace-instrument", ProcessorKind::replacement, "Replace with " + instrumentName, true,
            {{"timing_preservation", 1.0}, {"dynamics_preservation", 1.0}, {"timbre_mix", 1.0}}};
}

std::string AiRequestBuilder::separation(const std::filesystem::path& source,
                                         const std::string& quality,
                                         const std::vector<std::string>& stems) {
    std::ostringstream out;
    out << "{\"id\":\"separate-1\",\"method\":\"separation.run\",\"params\":{\"source\":\""
        << escapeJson(source.string()) << "\",\"quality\":\"" << escapeJson(quality) << "\",\"stems\":[";
    for (std::size_t i = 0; i < stems.size(); ++i) { if (i) out << ','; out << '"' << escapeJson(stems[i]) << '"'; }
    out << "]}}";
    return out.str();
}

std::string AiRequestBuilder::transcription(const std::string& stemId, bool includePitchBends) {
    return "{\"id\":\"transcribe-1\",\"method\":\"transcription.run\",\"params\":{\"stemId\":\"" +
           escapeJson(stemId) + "\",\"includePitchBends\":" + (includePitchBends ? "true" : "false") + "}}";
}

std::string AiRequestBuilder::assistantPlan(const std::string& instruction) {
    return "{\"id\":\"assistant-1\",\"method\":\"assistant.plan\",\"params\":{\"instruction\":\"" +
           escapeJson(instruction) + "\"}}";
}

std::string toString(StemRole role) {
    switch (role) {
        case StemRole::leadVocal: return "lead-vocal";
        case StemRole::backingVocal: return "backing-vocal";
        case StemRole::drums: return "drums";
        case StemRole::bass: return "bass";
        case StemRole::guitar: return "guitar";
        case StemRole::piano: return "piano";
        case StemRole::strings: return "strings";
        case StemRole::synth: return "synth";
        case StemRole::effects: return "effects";
        case StemRole::other: return "other";
    }
    return "other";
}

std::string toString(JobState state) {
    switch (state) {
        case JobState::queued: return "queued";
        case JobState::running: return "running";
        case JobState::completed: return "completed";
        case JobState::failed: return "failed";
        case JobState::cancelled: return "cancelled";
    }
    return "unknown";
}

} // namespace omnistem
