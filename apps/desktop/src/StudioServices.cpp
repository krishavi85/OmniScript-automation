#include "StudioServices.h"

#include <algorithm>

namespace omnistem::desktop {

namespace {

int levelRank(const juce::String& level) {
    if (level.equalsIgnoreCase("error")) return 4;
    if (level.equalsIgnoreCase("warning")) return 3;
    if (level.equalsIgnoreCase("info")) return 2;
    return 1;
}

juce::String payloadMessage(const juce::var& value) {
    if (auto* object = value.getDynamicObject()) {
        const auto direct = object->getProperty("message").toString();
        if (direct.isNotEmpty()) return direct;
        const auto errorValue = object->getProperty("error");
        if (auto* error = errorValue.getDynamicObject())
            return error->getProperty("message").toString();
    }
    return {};
}

} // namespace

void LogStore::add(juce::String level, juce::String category, juce::String message) {
    {
        std::scoped_lock lock(mutex);
        records.push_back({juce::Time::getCurrentTime(), std::move(level),
                           std::move(category), std::move(message)});
        while (records.size() > 5000)
            records.pop_front();
    }
    sendChangeMessage();
}

std::vector<LogEntry> LogStore::entries() const {
    std::scoped_lock lock(mutex);
    return {records.begin(), records.end()};
}

std::vector<LogEntry> LogStore::search(const juce::String& query,
                                       const juce::String& minimumLevel) const {
    const auto needle = query.trim().toLowerCase();
    const auto minimum = minimumLevel.isEmpty() ? 0 : levelRank(minimumLevel);
    std::vector<LogEntry> matches;
    std::scoped_lock lock(mutex);
    for (const auto& entry : records) {
        if (levelRank(entry.level) < minimum) continue;
        const auto text = (entry.level + " " + entry.category + " " + entry.message).toLowerCase();
        if (needle.isEmpty() || text.contains(needle))
            matches.push_back(entry);
    }
    return matches;
}

void LogStore::clear() {
    {
        std::scoped_lock lock(mutex);
        records.clear();
    }
    sendChangeMessage();
}

StudioState::StudioState(LogStore& logs) : logStore(logs) {
    juce::PropertiesFile::Options options;
    options.applicationName = "OmniStemStudio";
    options.filenameSuffix = ".settings";
    options.folderName = "OmniStemStudio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    settings.setStorageParameters(options);

    currentProject.id = juce::Uuid().toString().toStdString();
    currentProject.name = "Untitled OmniStem Project";
    loadSettings();
}

void StudioState::setCurrentAudio(const juce::File& file) {
    if (file == currentAudio) return;
    currentAudio = file;
    if (file.existsAsFile()) {
        currentProject.name = file.getFileNameWithoutExtension().toStdString();
        logStore.add("info", "project", "Loaded source audio: " + file.getFullPathName());
    }
    sendChangeMessage();
}

void StudioState::setOutputDirectory(const juce::File& directory) {
    if (directory == outputDirectory) return;
    outputDirectory = directory;
    if (!outputDirectory.exists())
        outputDirectory.createDirectory();
    saveSettings();
    sendChangeMessage();
}

void StudioState::setApiUrl(juce::String url) {
    url = url.trim();
    while (url.endsWithChar('/')) url = url.dropLastCharacters(1);
    if (url.isEmpty() || url == apiUrl) return;
    apiUrl = std::move(url);
    saveSettings();
    logStore.add("info", "settings", "Worker API set to " + apiUrl);
    sendChangeMessage();
}

void StudioState::saveSettings() {
    auto& file = settings.getUserSettings();
    file.setValue("apiUrl", apiUrl);
    file.setValue("outputDirectory", outputDirectory.getFullPathName());
    file.saveIfNeeded();
}

void StudioState::loadSettings() {
    auto& file = settings.getUserSettings();
    apiUrl = file.getValue("apiUrl", "http://127.0.0.1:8765");
    const auto defaultOutput = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                   .getChildFile("OmniStem Studio")
                                   .getChildFile("Outputs");
    outputDirectory = juce::File(file.getValue("outputDirectory", defaultOutput.getFullPathName()));
    outputDirectory.createDirectory();
}

WorkerService::WorkerService(StudioState& state, LogStore& logs)
    : juce::Thread("OmniStem worker service"), studioState(state), logStore(logs) {
    startThread(juce::Thread::Priority::normal);
}

WorkerService::~WorkerService() {
    signalThreadShouldExit();
    queueEvent.signal();
    stopThread(10000);
    cancelPendingUpdate();
}

juce::String WorkerService::submit(juce::String title,
                                   juce::String method,
                                   juce::var params) {
    auto request = std::make_shared<Request>();
    request->localId = juce::Uuid().toString();
    request->title = std::move(title);
    request->method = std::move(method);
    request->params = std::move(params);

    RemoteJobSnapshot snapshot;
    snapshot.localId = request->localId;
    snapshot.title = request->title;
    snapshot.method = request->method;
    snapshot.state = RemoteJobState::queued;
    snapshot.message = "Queued";
    snapshot.startedAt = juce::Time::getCurrentTime();

    {
        std::scoped_lock lock(mutex);
        requests[request->localId] = request;
        jobs[request->localId] = snapshot;
        queue.push_back(request);
    }
    logStore.add("info", "worker", "Queued " + request->title);
    triggerAsyncUpdate();
    queueEvent.signal();
    return request->localId;
}

bool WorkerService::cancel(const juce::String& localId) {
    std::shared_ptr<Request> request;
    {
        std::scoped_lock lock(mutex);
        const auto found = requests.find(localId);
        if (found == requests.end()) return false;
        request = found->second;
        request->cancelRequested.store(true);
    }
    logStore.add("warning", "worker", "Cancellation requested for " + localId);
    queueEvent.signal();
    return true;
}

std::vector<RemoteJobSnapshot> WorkerService::snapshots() const {
    std::scoped_lock lock(mutex);
    std::vector<RemoteJobSnapshot> result;
    result.reserve(jobs.size());
    for (const auto& [_, snapshot] : jobs)
        result.push_back(snapshot);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.startedAt > right.startedAt;
    });
    return result;
}

std::optional<RemoteJobSnapshot> WorkerService::snapshot(const juce::String& localId) const {
    std::scoped_lock lock(mutex);
    const auto found = jobs.find(localId);
    if (found == jobs.end()) return std::nullopt;
    return found->second;
}

bool WorkerService::isBusy() const {
    std::scoped_lock lock(mutex);
    return std::any_of(jobs.begin(), jobs.end(), [](const auto& pair) {
        return pair.second.state == RemoteJobState::queued || pair.second.state == RemoteJobState::running;
    });
}

void WorkerService::run() {
    while (!threadShouldExit()) {
        std::shared_ptr<Request> request;
        {
            std::scoped_lock lock(mutex);
            if (!queue.empty()) {
                request = queue.front();
                queue.pop_front();
            }
        }
        if (request) {
            process(request);
            continue;
        }
        queueEvent.wait(250);
        queueEvent.reset();
    }
}

void WorkerService::handleAsyncUpdate() {
    sendChangeMessage();
}

void WorkerService::process(const std::shared_ptr<Request>& request) {
    if (request->cancelRequested.load()) {
        update(request->localId, RemoteJobState::cancelled, 0.0, "Cancelled before execution");
        return;
    }

    update(request->localId, RemoteJobState::running, 0.01, "Contacting worker API");
    WorkerApiClient client(studioState.getApiUrl());
    const auto response = client.rpc(request->method, request->params, 30000);
    if (!response.ok) {
        update(request->localId, RemoteJobState::failed, 0.0, response.message, response.payload);
        return;
    }

    const auto result = objectProperty(response.payload, "result");
    const auto workerJobId = objectProperty(result, "jobId").toString();
    if (workerJobId.isEmpty()) {
        update(request->localId, RemoteJobState::completed, 1.0, "Completed", result);
        return;
    }

    {
        std::scoped_lock lock(mutex);
        jobs[request->localId].workerJobId = workerJobId;
    }
    triggerAsyncUpdate();

    bool cancellationSent = false;
    while (!threadShouldExit()) {
        if (request->cancelRequested.load() && !cancellationSent) {
            client.cancelJob(workerJobId);
            cancellationSent = true;
        }

        const auto status = client.jobStatus(workerJobId, 5000);
        if (!status.ok) {
            update(request->localId, RemoteJobState::failed, 0.0, status.message, status.payload);
            return;
        }

        const auto stateText = objectProperty(status.payload, "state").toString();
        const auto state = parseState(stateText);
        const auto progress = static_cast<double>(objectProperty(status.payload, "progress"));
        auto message = objectProperty(status.payload, "message").toString();
        if (message.isEmpty()) message = stateText;

        if (state == RemoteJobState::completed) {
            update(request->localId, state, 1.0, message,
                   objectProperty(status.payload, "result"));
            return;
        }
        if (state == RemoteJobState::failed || state == RemoteJobState::cancelled) {
            const auto error = objectProperty(status.payload, "error");
            const auto detail = payloadMessage(error);
            update(request->localId, state, progress,
                   detail.isNotEmpty() ? detail : message, status.payload);
            return;
        }

        update(request->localId, state, progress, message, status.payload);
        wait(250);
    }
}

void WorkerService::update(const juce::String& localId,
                           RemoteJobState state,
                           double progress,
                           juce::String message,
                           juce::var payload) {
    bool terminal = false;
    juce::String title;
    {
        std::scoped_lock lock(mutex);
        auto found = jobs.find(localId);
        if (found == jobs.end()) return;
        auto& snapshot = found->second;
        snapshot.state = state;
        snapshot.progress = juce::jlimit(0.0, 1.0, progress);
        snapshot.message = std::move(message);
        if (!payload.isVoid()) snapshot.payload = std::move(payload);
        terminal = state == RemoteJobState::completed || state == RemoteJobState::failed
                   || state == RemoteJobState::cancelled;
        if (terminal) snapshot.finishedAt = juce::Time::getCurrentTime();
        title = snapshot.title;
    }

    if (terminal) {
        const auto level = state == RemoteJobState::completed ? "info"
                         : state == RemoteJobState::cancelled ? "warning" : "error";
        logStore.add(level, "worker", title + ": " + toString(state));
    }
    triggerAsyncUpdate();
}

RemoteJobState WorkerService::parseState(const juce::String& value) {
    if (value == "running") return RemoteJobState::running;
    if (value == "completed") return RemoteJobState::completed;
    if (value == "failed") return RemoteJobState::failed;
    if (value == "cancelled") return RemoteJobState::cancelled;
    return RemoteJobState::queued;
}

juce::var WorkerService::objectProperty(const juce::var& value,
                                        const juce::Identifier& name) {
    if (auto* object = value.getDynamicObject())
        return object->getProperty(name);
    return {};
}

juce::String toString(RemoteJobState state) {
    switch (state) {
        case RemoteJobState::queued: return "queued";
        case RemoteJobState::running: return "running";
        case RemoteJobState::completed: return "completed";
        case RemoteJobState::failed: return "failed";
        case RemoteJobState::cancelled: return "cancelled";
    }
    return "unknown";
}

} // namespace omnistem::desktop
