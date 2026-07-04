#pragma once

#include "WorkerApiClient.h"
#include "omnistem/core/OmniStemCore.h"

#include <JuceHeader.h>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace omnistem::desktop {

enum class RemoteJobState { queued, running, completed, failed, cancelled };

struct LogEntry {
    juce::Time timestamp;
    juce::String level;
    juce::String category;
    juce::String message;
};

struct RemoteJobSnapshot {
    juce::String localId;
    juce::String workerJobId;
    juce::String title;
    juce::String method;
    RemoteJobState state{RemoteJobState::queued};
    double progress{};
    juce::String message;
    juce::var payload;
    juce::Time startedAt;
    juce::Time finishedAt;
};

class LogStore final : public juce::ChangeBroadcaster {
public:
    void add(juce::String level, juce::String category, juce::String message);
    std::vector<LogEntry> entries() const;
    std::vector<LogEntry> search(const juce::String& query,
                                 const juce::String& minimumLevel = {}) const;
    void clear();

private:
    mutable std::mutex mutex;
    std::deque<LogEntry> records;
};

class StudioState final : public juce::ChangeBroadcaster {
public:
    explicit StudioState(LogStore& logs);

    void setCurrentAudio(const juce::File& file);
    const juce::File& getCurrentAudio() const noexcept { return currentAudio; }

    void setOutputDirectory(const juce::File& directory);
    const juce::File& getOutputDirectory() const noexcept { return outputDirectory; }

    void setApiUrl(juce::String url);
    const juce::String& getApiUrl() const noexcept { return apiUrl; }

    omnistem::Project& project() noexcept { return currentProject; }
    const omnistem::Project& project() const noexcept { return currentProject; }

    void saveSettings();
    void loadSettings();
    juce::PropertiesFile& properties() noexcept { return settings.getUserSettings(); }

private:
    LogStore& logStore;
    juce::ApplicationProperties settings;
    juce::File currentAudio;
    juce::File outputDirectory;
    juce::String apiUrl{"http://127.0.0.1:8765"};
    omnistem::Project currentProject;
};

class WorkerService final : private juce::Thread,
                            private juce::AsyncUpdater,
                            public juce::ChangeBroadcaster {
public:
    WorkerService(StudioState& state, LogStore& logs);
    ~WorkerService() override;

    juce::String submit(juce::String title,
                        juce::String method,
                        juce::var params);
    bool cancel(const juce::String& localId);
    std::vector<RemoteJobSnapshot> snapshots() const;
    std::optional<RemoteJobSnapshot> snapshot(const juce::String& localId) const;
    bool isBusy() const;

private:
    struct Request {
        juce::String localId;
        juce::String title;
        juce::String method;
        juce::var params;
        std::atomic_bool cancelRequested{false};
    };

    void run() override;
    void handleAsyncUpdate() override;
    void process(const std::shared_ptr<Request>& request);
    void update(const juce::String& localId,
                RemoteJobState state,
                double progress,
                juce::String message,
                juce::var payload = {});
    static RemoteJobState parseState(const juce::String& value);
    static juce::var objectProperty(const juce::var& value, const juce::Identifier& name);

    StudioState& studioState;
    LogStore& logStore;
    mutable std::mutex mutex;
    juce::WaitableEvent queueEvent;
    std::deque<std::shared_ptr<Request>> queue;
    std::map<juce::String, std::shared_ptr<Request>> requests;
    std::map<juce::String, RemoteJobSnapshot> jobs;
};

juce::String toString(RemoteJobState state);

} // namespace omnistem::desktop
