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
    explicit ProcessSupervisor(WorkerPolicy policy = {});

    bool registerWorker(std::string id, std::string& error);
    bool markStarted(const std::string& id, std::string& error);
    bool heartbeat(const std::string& id, std::string& error);
    bool markExited(const std::string& id, int exitCode, std::string& error);
    bool requestRestart(const std::string& id, std::string& error);
    void poll();

    std::optional<WorkerSnapshot> snapshot(const std::string& id) const;
    const std::map<std::string, WorkerSnapshot>& snapshots() const noexcept;

private:
    WorkerPolicy policy_;
    std::map<std::string, WorkerSnapshot> workers_;
    std::map<std::string, std::chrono::steady_clock::time_point> restartWindowStart_;
};

std::string toString(WorkerState state);

} // namespace omnistem::isolation
