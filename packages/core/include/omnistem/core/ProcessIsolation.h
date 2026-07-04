#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace omnistem::isolation {

enum class WorkerState { stopped, starting, healthy, unresponsive, crashed, quarantined };

struct WorkerPolicy {
    std::chrono::milliseconds heartbeatTimeout{2000};
    std::chrono::milliseconds restartWindow{60000};
    std::uint32_t maximumRestarts{3};
};

struct WorkerSnapshot {
    std::string id;
    WorkerState state{WorkerState::stopped};
    std::uint64_t generation{};
    std::uint32_t restartCount{};
    std::chrono::steady_clock::time_point lastHeartbeat{};
    std::string message;
};

class ProcessSupervisor {
public:
    explicit ProcessSupervisor(WorkerPolicy policy = {}) : policy_(policy) {}

    bool registerWorker(std::string id, std::string& error) {
        if (id.empty()) { error = "Worker ID must not be empty"; return false; }
        if (workers_.contains(id)) { error = "Worker is already registered"; return false; }
        WorkerSnapshot value;
        value.id = std::move(id);
        restartWindowStart_[value.id] = std::chrono::steady_clock::now();
        workers_.emplace(value.id, value);
        return true;
    }

    bool markStarted(const std::string& id, std::string& error) {
        const auto it = workers_.find(id);
        if (it == workers_.end()) { error = "Unknown worker"; return false; }
        if (it->second.state == WorkerState::quarantined) { error = "Worker is quarantined"; return false; }
        ++it->second.generation;
        it->second.state = WorkerState::healthy;
        it->second.lastHeartbeat = std::chrono::steady_clock::now();
        it->second.message.clear();
        return true;
    }

    bool heartbeat(const std::string& id, std::string& error) {
        const auto it = workers_.find(id);
        if (it == workers_.end()) { error = "Unknown worker"; return false; }
        if (it->second.state == WorkerState::quarantined || it->second.state == WorkerState::stopped) {
            error = "Worker is not running";
            return false;
        }
        it->second.state = WorkerState::healthy;
        it->second.lastHeartbeat = std::chrono::steady_clock::now();
        return true;
    }

    bool markExited(const std::string& id, int exitCode, std::string& error) {
        const auto it = workers_.find(id);
        if (it == workers_.end()) { error = "Unknown worker"; return false; }
        it->second.state = exitCode == 0 ? WorkerState::stopped : WorkerState::crashed;
        it->second.message = "Process exited with code " + std::to_string(exitCode);
        return true;
    }

    bool requestRestart(const std::string& id, std::string& error) {
        const auto it = workers_.find(id);
        if (it == workers_.end()) { error = "Unknown worker"; return false; }
        const auto now = std::chrono::steady_clock::now();
        auto& windowStart = restartWindowStart_[id];
        if (now - windowStart > policy_.restartWindow) {
            windowStart = now;
            it->second.restartCount = 0;
        }
        if (it->second.restartCount >= policy_.maximumRestarts) {
            it->second.state = WorkerState::quarantined;
            it->second.message = "Restart budget exhausted";
            error = it->second.message;
            return false;
        }
        ++it->second.restartCount;
        it->second.state = WorkerState::starting;
        it->second.message = "Restart requested";
        return true;
    }

    void poll() {
        const auto now = std::chrono::steady_clock::now();
        for (auto& [_, worker] : workers_) {
            if (worker.state == WorkerState::healthy && now - worker.lastHeartbeat > policy_.heartbeatTimeout) {
                worker.state = WorkerState::unresponsive;
                worker.message = "Heartbeat timeout";
            }
        }
    }

    std::optional<WorkerSnapshot> snapshot(const std::string& id) const {
        const auto it = workers_.find(id);
        return it == workers_.end() ? std::nullopt : std::optional<WorkerSnapshot>(it->second);
    }

    const std::map<std::string, WorkerSnapshot>& snapshots() const noexcept { return workers_; }

private:
    WorkerPolicy policy_;
    std::map<std::string, WorkerSnapshot> workers_;
    std::map<std::string, std::chrono::steady_clock::time_point> restartWindowStart_;
};

inline std::string toString(WorkerState state) {
    switch (state) {
        case WorkerState::stopped: return "stopped";
        case WorkerState::starting: return "starting";
        case WorkerState::healthy: return "healthy";
        case WorkerState::unresponsive: return "unresponsive";
        case WorkerState::crashed: return "crashed";
        case WorkerState::quarantined: return "quarantined";
    }
    return "unknown";
}

} // namespace omnistem::isolation
