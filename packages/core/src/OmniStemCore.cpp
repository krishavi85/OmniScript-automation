#include "omnistem/core/OmniStemCore.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
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

StemRole stemRoleFromString(const std::string& role) {
    if (role == "lead-vocal") return StemRole::leadVocal;
    if (role == "backing-vocal") return StemRole::backingVocal;
    if (role == "drums") return StemRole::drums;
    if (role == "bass") return StemRole::bass;
    if (role == "guitar") return StemRole::guitar;
    if (role == "piano") return StemRole::piano;
    if (role == "strings") return StemRole::strings;
    if (role == "synth") return StemRole::synth;
    if (role == "effects") return StemRole::effects;
    return StemRole::other;
}

std::string processorKindToString(ProcessorKind kind) {
    switch (kind) {
        case ProcessorKind::restoration: return "restoration";
        case ProcessorKind::mastering: return "mastering";
        case ProcessorKind::replacement: return "replacement";
        case ProcessorKind::plugin: return "plugin";
    }
    return "plugin";
}

ProcessorKind processorKindFromString(const std::string& kind) {
    if (kind == "restoration") return ProcessorKind::restoration;
    if (kind == "mastering") return ProcessorKind::mastering;
    if (kind == "replacement") return ProcessorKind::replacement;
    return ProcessorKind::plugin;
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

class Statement {
public:
    Statement() = default;
    ~Statement() { if (value_) sqlite3_finalize(value_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    sqlite3_stmt** out() { return &value_; }
    sqlite3_stmt* get() const { return value_; }
private:
    sqlite3_stmt* value_{};
};

bool execSql(sqlite3* db, const char* sql, std::string& error) {
    char* rawError = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &rawError) != SQLITE_OK) {
        error = rawError ? rawError : "SQLite operation failed";
        sqlite3_free(rawError);
        return false;
    }
    return true;
}

bool prepare(sqlite3* db, const char* sql, Statement& statement, std::string& error) {
    if (sqlite3_prepare_v2(db, sql, -1, statement.out(), nullptr) == SQLITE_OK) return true;
    error = sqlite3_errmsg(db);
    return false;
}

bool bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string columnText(sqlite3_stmt* statement, int index) {
    const auto* text = sqlite3_column_text(statement, index);
    return text ? reinterpret_cast<const char*>(text) : std::string{};
}

bool tableExists(sqlite3* db, const std::string& tableName, std::string& error) {
    Statement statement;
    if (!prepare(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", statement, error)) return false;
    if (!bindText(statement.get(), 1, tableName)) {
        error = sqlite3_errmsg(db);
        return false;
    }
    return sqlite3_step(statement.get()) == SQLITE_ROW;
}

class Transaction {
public:
    Transaction(sqlite3* db, std::string& error) : db_(db), active_(execSql(db_, "BEGIN IMMEDIATE;", error)) {}
    ~Transaction() { if (active_) { std::string ignored; execSql(db_, "ROLLBACK;", ignored); } }
    bool active() const { return active_; }
    bool commit(std::string& error) {
        if (!active_) return false;
        if (!execSql(db_, "COMMIT;", error)) return false;
        active_ = false;
        return true;
    }
private:
    sqlite3* db_{};
    bool active_{};
};

bool migrateLegacyTables(sqlite3* db, std::string& error) {
    if (tableExists(db, "stems", error)) {
        if (!execSql(db,
            "INSERT OR IGNORE INTO project_stems(project_id,id,name,role,audio_path,gain_db,pan,muted,solo) "
            "SELECT project_id,id,name,role,audio_path,gain_db,pan,muted,solo FROM stems;", error)) return false;
    }
    if (tableExists(db, "notes", error)) {
        if (!execSql(db,
            "INSERT OR IGNORE INTO project_notes(project_id,id,stem_id,start_seconds,duration_seconds,gain_db,pan,formant,confidence,muted,pitch_curve) "
            "SELECT project_id,id,stem_id,start_seconds,duration_seconds,gain_db,pan,formant,confidence,muted,pitch_curve FROM notes;", error)) return false;
    }
    if (tableExists(db, "spectral_masks", error)) {
        if (!execSql(db,
            "INSERT OR IGNORE INTO project_masks(project_id,id,source_stem_id,destination_stem_id,start_seconds,end_seconds,low_hz,high_hz,gain_db,feather) "
            "SELECT project_id,id,source_stem_id,destination_stem_id,start_seconds,end_seconds,low_hz,high_hz,gain_db,feather FROM spectral_masks;", error)) return false;
    }
    return true;
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
    explicit Impl(std::filesystem::path databasePath) : path(std::move(databasePath)) {}
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
        error = impl_->db ? sqlite3_errmsg(impl_->db) : "Unable to open SQLite database";
        return false;
    }
    constexpr const char* schema = R"SQL(
        PRAGMA foreign_keys=ON;
        CREATE TABLE IF NOT EXISTS projects(
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            sample_rate REAL NOT NULL
        );
        CREATE TABLE IF NOT EXISTS project_stems(
            project_id TEXT NOT NULL,
            id TEXT NOT NULL,
            name TEXT NOT NULL,
            role TEXT NOT NULL,
            audio_path TEXT NOT NULL,
            gain_db REAL NOT NULL,
            pan REAL NOT NULL,
            muted INTEGER NOT NULL,
            solo INTEGER NOT NULL,
            PRIMARY KEY(project_id,id),
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS project_notes(
            project_id TEXT NOT NULL,
            id TEXT NOT NULL,
            stem_id TEXT NOT NULL,
            start_seconds REAL NOT NULL,
            duration_seconds REAL NOT NULL,
            gain_db REAL NOT NULL,
            pan REAL NOT NULL,
            formant REAL NOT NULL,
            confidence REAL NOT NULL,
            muted INTEGER NOT NULL,
            pitch_curve TEXT NOT NULL,
            PRIMARY KEY(project_id,id),
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS note_gain_envelopes(
            project_id TEXT NOT NULL,
            note_id TEXT NOT NULL,
            point_index INTEGER NOT NULL,
            time_seconds REAL NOT NULL,
            value REAL NOT NULL,
            PRIMARY KEY(project_id,note_id,point_index),
            FOREIGN KEY(project_id,note_id) REFERENCES project_notes(project_id,id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS project_masks(
            project_id TEXT NOT NULL,
            id TEXT NOT NULL,
            source_stem_id TEXT NOT NULL,
            destination_stem_id TEXT NOT NULL,
            start_seconds REAL NOT NULL,
            end_seconds REAL NOT NULL,
            low_hz REAL NOT NULL,
            high_hz REAL NOT NULL,
            gain_db REAL NOT NULL,
            feather REAL NOT NULL,
            PRIMARY KEY(project_id,id),
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS project_processors(
            project_id TEXT NOT NULL,
            id TEXT NOT NULL,
            kind TEXT NOT NULL,
            name TEXT NOT NULL,
            enabled INTEGER NOT NULL,
            position INTEGER NOT NULL,
            PRIMARY KEY(project_id,id),
            FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS processor_parameters(
            project_id TEXT NOT NULL,
            processor_id TEXT NOT NULL,
            name TEXT NOT NULL,
            value REAL NOT NULL,
            PRIMARY KEY(project_id,processor_id,name),
            FOREIGN KEY(project_id,processor_id) REFERENCES project_processors(project_id,id) ON DELETE CASCADE
        );
        PRAGMA user_version=2;
    )SQL";
    if (!execSql(impl_->db, schema, error)) return false;
    return migrateLegacyTables(impl_->db, error);
#else
    error = "SQLite3 was not found when OmniStemCore was built";
    return false;
#endif
}

bool ProjectRepository::save(const Project& project, std::string& error) {
#if OMNISTEM_HAS_SQLITE
    if (project.id.empty()) {
        error = "Project ID must not be empty";
        return false;
    }
    if (!impl_->db && !open(error)) return false;
    Transaction transaction(impl_->db, error);
    if (!transaction.active()) return false;

    {
        Statement statement;
        if (!prepare(impl_->db,
            "INSERT INTO projects(id,name,sample_rate) VALUES(?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET name=excluded.name,sample_rate=excluded.sample_rate;",
            statement, error)) return false;
        if (!bindText(statement.get(), 1, project.id) ||
            !bindText(statement.get(), 2, project.name) ||
            sqlite3_bind_double(statement.get(), 3, project.sampleRate) != SQLITE_OK ||
            sqlite3_step(statement.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(impl_->db);
            return false;
        }
    }

    for (const char* table : {"processor_parameters", "note_gain_envelopes", "project_processors", "project_masks", "project_notes", "project_stems"}) {
        const std::string sql = std::string("DELETE FROM ") + table + " WHERE project_id=?;";
        Statement statement;
        if (!prepare(impl_->db, sql.c_str(), statement, error) ||
            !bindText(statement.get(), 1, project.id) ||
            sqlite3_step(statement.get()) != SQLITE_DONE) {
            if (error.empty()) error = sqlite3_errmsg(impl_->db);
            return false;
        }
    }

    for (const auto& stem : project.stems) {
        Statement statement;
        if (!prepare(impl_->db,
            "INSERT INTO project_stems(project_id,id,name,role,audio_path,gain_db,pan,muted,solo) VALUES(?,?,?,?,?,?,?,?,?);",
            statement, error)) return false;
        if (!bindText(statement.get(), 1, project.id) || !bindText(statement.get(), 2, stem.id) ||
            !bindText(statement.get(), 3, stem.name) || !bindText(statement.get(), 4, toString(stem.role)) ||
            !bindText(statement.get(), 5, stem.audioPath.string()) ||
            sqlite3_bind_double(statement.get(), 6, stem.gainDb) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 7, stem.pan) != SQLITE_OK ||
            sqlite3_bind_int(statement.get(), 8, stem.muted ? 1 : 0) != SQLITE_OK ||
            sqlite3_bind_int(statement.get(), 9, stem.solo ? 1 : 0) != SQLITE_OK ||
            sqlite3_step(statement.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(impl_->db);
            return false;
        }
    }

    for (const auto& note : project.notes) {
        Statement statement;
        if (!prepare(impl_->db,
            "INSERT INTO project_notes(project_id,id,stem_id,start_seconds,duration_seconds,gain_db,pan,formant,confidence,muted,pitch_curve) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?);", statement, error)) return false;
        if (!bindText(statement.get(), 1, project.id) || !bindText(statement.get(), 2, note.id) ||
            !bindText(statement.get(), 3, note.stemId) ||
            sqlite3_bind_double(statement.get(), 4, note.startSeconds) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 5, note.durationSeconds) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 6, note.gainDb) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 7, note.pan) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 8, note.formantSemitones) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 9, note.confidence) != SQLITE_OK ||
            sqlite3_bind_int(statement.get(), 10, note.muted ? 1 : 0) != SQLITE_OK ||
            !bindText(statement.get(), 11, serializePitch(note.pitchCurve)) ||
            sqlite3_step(statement.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(impl_->db);
            return false;
        }
        for (std::size_t index = 0; index < note.gainEnvelope.size(); ++index) {
            Statement envelopeStatement;
            if (!prepare(impl_->db,
                "INSERT INTO note_gain_envelopes(project_id,note_id,point_index,time_seconds,value) VALUES(?,?,?,?,?);",
                envelopeStatement, error)) return false;
            if (!bindText(envelopeStatement.get(), 1, project.id) || !bindText(envelopeStatement.get(), 2, note.id) ||
                sqlite3_bind_int64(envelopeStatement.get(), 3, static_cast<sqlite3_int64>(index)) != SQLITE_OK ||
                sqlite3_bind_double(envelopeStatement.get(), 4, note.gainEnvelope[index].timeSeconds) != SQLITE_OK ||
                sqlite3_bind_double(envelopeStatement.get(), 5, note.gainEnvelope[index].value) != SQLITE_OK ||
                sqlite3_step(envelopeStatement.get()) != SQLITE_DONE) {
                error = sqlite3_errmsg(impl_->db);
                return false;
            }
        }
    }

    for (const auto& mask : project.masks) {
        Statement statement;
        if (!prepare(impl_->db,
            "INSERT INTO project_masks(project_id,id,source_stem_id,destination_stem_id,start_seconds,end_seconds,low_hz,high_hz,gain_db,feather) "
            "VALUES(?,?,?,?,?,?,?,?,?,?);", statement, error)) return false;
        if (!bindText(statement.get(), 1, project.id) || !bindText(statement.get(), 2, mask.id) ||
            !bindText(statement.get(), 3, mask.sourceStemId) || !bindText(statement.get(), 4, mask.destinationStemId) ||
            sqlite3_bind_double(statement.get(), 5, mask.startSeconds) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 6, mask.endSeconds) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 7, mask.lowFrequencyHz) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 8, mask.highFrequencyHz) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 9, mask.gainDb) != SQLITE_OK ||
            sqlite3_bind_double(statement.get(), 10, mask.feather) != SQLITE_OK ||
            sqlite3_step(statement.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(impl_->db);
            return false;
        }
    }

    for (std::size_t position = 0; position < project.processingGraph.size(); ++position) {
        const auto& processor = project.processingGraph[position];
        Statement statement;
        if (!prepare(impl_->db,
            "INSERT INTO project_processors(project_id,id,kind,name,enabled,position) VALUES(?,?,?,?,?,?);",
            statement, error)) return false;
        if (!bindText(statement.get(), 1, project.id) || !bindText(statement.get(), 2, processor.id) ||
            !bindText(statement.get(), 3, processorKindToString(processor.kind)) || !bindText(statement.get(), 4, processor.name) ||
            sqlite3_bind_int(statement.get(), 5, processor.enabled ? 1 : 0) != SQLITE_OK ||
            sqlite3_bind_int64(statement.get(), 6, static_cast<sqlite3_int64>(position)) != SQLITE_OK ||
            sqlite3_step(statement.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(impl_->db);
            return false;
        }
        for (const auto& [name, value] : processor.parameters) {
            Statement parameterStatement;
            if (!prepare(impl_->db,
                "INSERT INTO processor_parameters(project_id,processor_id,name,value) VALUES(?,?,?,?);",
                parameterStatement, error)) return false;
            if (!bindText(parameterStatement.get(), 1, project.id) ||
                !bindText(parameterStatement.get(), 2, processor.id) ||
                !bindText(parameterStatement.get(), 3, name) ||
                sqlite3_bind_double(parameterStatement.get(), 4, value) != SQLITE_OK ||
                sqlite3_step(parameterStatement.get()) != SQLITE_DONE) {
                error = sqlite3_errmsg(impl_->db);
                return false;
            }
        }
    }

    return transaction.commit(error);
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

    {
        Statement statement;
        if (!prepare(impl_->db, "SELECT id,name,sample_rate FROM projects WHERE id=?;", statement, error) ||
            !bindText(statement.get(), 1, projectId)) {
            if (error.empty()) error = sqlite3_errmsg(impl_->db);
            return std::nullopt;
        }
        if (sqlite3_step(statement.get()) != SQLITE_ROW) {
            error = "Project not found";
            return std::nullopt;
        }
        project.id = columnText(statement.get(), 0);
        project.name = columnText(statement.get(), 1);
        project.sampleRate = sqlite3_column_double(statement.get(), 2);
    }

    {
        Statement statement;
        if (!prepare(impl_->db,
            "SELECT id,name,role,audio_path,gain_db,pan,muted,solo FROM project_stems WHERE project_id=? ORDER BY rowid;",
            statement, error) || !bindText(statement.get(), 1, projectId)) return std::nullopt;
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            Stem stem;
            stem.id = columnText(statement.get(), 0);
            stem.name = columnText(statement.get(), 1);
            stem.role = stemRoleFromString(columnText(statement.get(), 2));
            stem.audioPath = columnText(statement.get(), 3);
            stem.gainDb = sqlite3_column_double(statement.get(), 4);
            stem.pan = sqlite3_column_double(statement.get(), 5);
            stem.muted = sqlite3_column_int(statement.get(), 6) != 0;
            stem.solo = sqlite3_column_int(statement.get(), 7) != 0;
            project.stems.push_back(std::move(stem));
        }
    }

    std::unordered_map<std::string, std::size_t> noteIndexes;
    {
        Statement statement;
        if (!prepare(impl_->db,
            "SELECT id,stem_id,start_seconds,duration_seconds,gain_db,pan,formant,confidence,muted,pitch_curve "
            "FROM project_notes WHERE project_id=? ORDER BY rowid;",
            statement, error) || !bindText(statement.get(), 1, projectId)) return std::nullopt;
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            NoteEvent note;
            note.id = columnText(statement.get(), 0);
            note.stemId = columnText(statement.get(), 1);
            note.startSeconds = sqlite3_column_double(statement.get(), 2);
            note.durationSeconds = sqlite3_column_double(statement.get(), 3);
            note.gainDb = sqlite3_column_double(statement.get(), 4);
            note.pan = sqlite3_column_double(statement.get(), 5);
            note.formantSemitones = sqlite3_column_double(statement.get(), 6);
            note.confidence = sqlite3_column_double(statement.get(), 7);
            note.muted = sqlite3_column_int(statement.get(), 8) != 0;
            note.pitchCurve = parsePitch(columnText(statement.get(), 9));
            noteIndexes.emplace(note.id, project.notes.size());
            project.notes.push_back(std::move(note));
        }
    }

    {
        Statement statement;
        if (!prepare(impl_->db,
            "SELECT note_id,time_seconds,value FROM note_gain_envelopes WHERE project_id=? ORDER BY note_id,point_index;",
            statement, error) || !bindText(statement.get(), 1, projectId)) return std::nullopt;
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            const auto noteId = columnText(statement.get(), 0);
            const auto it = noteIndexes.find(noteId);
            if (it != noteIndexes.end()) {
                project.notes[it->second].gainEnvelope.push_back({
                    sqlite3_column_double(statement.get(), 1), sqlite3_column_double(statement.get(), 2)});
            }
        }
    }

    {
        Statement statement;
        if (!prepare(impl_->db,
            "SELECT id,source_stem_id,destination_stem_id,start_seconds,end_seconds,low_hz,high_hz,gain_db,feather "
            "FROM project_masks WHERE project_id=? ORDER BY rowid;",
            statement, error) || !bindText(statement.get(), 1, projectId)) return std::nullopt;
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            project.masks.push_back({
                columnText(statement.get(), 0), columnText(statement.get(), 1), columnText(statement.get(), 2),
                sqlite3_column_double(statement.get(), 3), sqlite3_column_double(statement.get(), 4),
                sqlite3_column_double(statement.get(), 5), sqlite3_column_double(statement.get(), 6),
                sqlite3_column_double(statement.get(), 7), sqlite3_column_double(statement.get(), 8)});
        }
    }

    std::unordered_map<std::string, std::size_t> processorIndexes;
    {
        Statement statement;
        if (!prepare(impl_->db,
            "SELECT id,kind,name,enabled FROM project_processors WHERE project_id=? ORDER BY position;",
            statement, error) || !bindText(statement.get(), 1, projectId)) return std::nullopt;
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            ProcessorNode processor;
            processor.id = columnText(statement.get(), 0);
            processor.kind = processorKindFromString(columnText(statement.get(), 1));
            processor.name = columnText(statement.get(), 2);
            processor.enabled = sqlite3_column_int(statement.get(), 3) != 0;
            processorIndexes.emplace(processor.id, project.processingGraph.size());
            project.processingGraph.push_back(std::move(processor));
        }
    }

    {
        Statement statement;
        if (!prepare(impl_->db,
            "SELECT processor_id,name,value FROM processor_parameters WHERE project_id=? ORDER BY processor_id,name;",
            statement, error) || !bindText(statement.get(), 1, projectId)) return std::nullopt;
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            const auto processorId = columnText(statement.get(), 0);
            const auto it = processorIndexes.find(processorId);
            if (it != processorIndexes.end()) {
                project.processingGraph[it->second].parameters[columnText(statement.get(), 1)] =
                    sqlite3_column_double(statement.get(), 2);
            }
        }
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
    if (stopping_ || spec.id.empty() || !work || jobs_.contains(spec.id)) return false;
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
    if (it->second->snapshot.state == JobState::queued) {
        it->second->snapshot.state = JobState::cancelled;
        it->second->snapshot.message = "Cancelled before execution";
    }
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
            control = queue_.front();
            queue_.pop();
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
    if (!(tempoBpm > 0.0) || !std::isfinite(tempoBpm) || ticksPerQuarter == 0) {
        error = "Invalid MIDI timing settings";
        return false;
    }
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
    appendVarLen(track, 0);
    track.insert(track.end(), {0xff, 0x51, 0x03,
        static_cast<std::uint8_t>((microsPerQuarter >> 16) & 0xff),
        static_cast<std::uint8_t>((microsPerQuarter >> 8) & 0xff),
        static_cast<std::uint8_t>(microsPerQuarter & 0xff)});
    std::uint32_t previousTick{};
    for (const auto& event : events) {
        appendVarLen(track, event.tick - previousTick);
        track.push_back(event.on ? 0x90 : 0x80);
        track.push_back(event.note);
        track.push_back(event.velocity);
        previousTick = event.tick;
    }
    appendVarLen(track, 0);
    track.insert(track.end(), {0xff, 0x2f, 0x00});

    std::vector<std::uint8_t> file;
    file.insert(file.end(), {'M','T','h','d'});
    appendBe32(file, 6);
    appendBe16(file, 1);
    appendBe16(file, 1);
    appendBe16(file, ticksPerQuarter);
    file.insert(file.end(), {'M','T','r','k'});
    appendBe32(file, static_cast<std::uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "Cannot open MIDI output file";
        return false;
    }
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!output) {
        error = "Failed while writing MIDI output";
        return false;
    }
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
    for (std::size_t i = 0; i < stems.size(); ++i) {
        if (i != 0) out << ',';
        out << '"' << escapeJson(stems[i]) << '"';
    }
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
