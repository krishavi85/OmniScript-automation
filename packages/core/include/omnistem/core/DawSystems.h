#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace omnistem::daw {

struct TempoPoint {
    double beat{};
    double bpm{120.0};
};

class TempoMap {
public:
    TempoMap();
    bool addPoint(TempoPoint point, std::string& error);
    double secondsAtBeat(double beat) const;
    double beatAtSeconds(double seconds) const;
    const std::vector<TempoPoint>& points() const noexcept;

private:
    std::vector<TempoPoint> points_;
};

struct AutomationPoint {
    double timeSeconds{};
    double value{};
};

class AutomationLane {
public:
    bool addPoint(AutomationPoint point, std::string& error);
    double valueAt(double timeSeconds, double fallback = 0.0) const;
    const std::vector<AutomationPoint>& points() const noexcept;

private:
    std::vector<AutomationPoint> points_;
};

struct Take {
    std::string id;
    std::filesystem::path source;
    double sourceOffsetSeconds{};
    double lengthSeconds{};
};

struct CompRegion {
    double startSeconds{};
    double endSeconds{};
    std::string takeId;
};

class CompLane {
public:
    bool addTake(Take take, std::string& error);
    bool addRegion(CompRegion region, std::string& error);
    std::optional<Take> selectedTakeAt(double timeSeconds) const;
    const std::vector<Take>& takes() const noexcept;
    const std::vector<CompRegion>& regions() const noexcept;

private:
    std::vector<Take> takes_;
    std::vector<CompRegion> regions_;
};

struct RouteNode {
    std::string id;
    std::uint32_t intrinsicLatencySamples{};
};

struct RouteEdge {
    std::string source;
    std::string destination;
};

class RoutingGraph {
public:
    bool addNode(RouteNode node, std::string& error);
    bool connect(RouteEdge edge, std::string& error);
    std::vector<std::string> topologicalOrder(std::string& error) const;
    std::map<std::string, std::uint64_t> accumulatedLatency(std::string& error) const;
    std::map<std::string, std::uint64_t> outputCompensation(std::string& error) const;
    const std::vector<RouteNode>& nodes() const noexcept;
    const std::vector<RouteEdge>& edges() const noexcept;

private:
    std::vector<RouteNode> nodes_;
    std::vector<RouteEdge> edges_;
};

struct PluginState {
    std::string instanceId;
    std::string format;
    std::string identifier;
    std::uint32_t latencySamples{};
    bool bypassed{};
    std::vector<std::uint8_t> state;
};

class PluginChain {
public:
    bool insert(std::size_t index, PluginState plugin, std::string& error);
    bool remove(const std::string& instanceId);
    PluginState* find(const std::string& instanceId);
    const PluginState* find(const std::string& instanceId) const;
    std::uint64_t totalLatencySamples() const noexcept;
    const std::vector<PluginState>& plugins() const noexcept;

private:
    std::vector<PluginState> plugins_;
};

class WavFloatRecorder {
public:
    WavFloatRecorder() = default;
    ~WavFloatRecorder();
    WavFloatRecorder(const WavFloatRecorder&) = delete;
    WavFloatRecorder& operator=(const WavFloatRecorder&) = delete;

    bool open(const std::filesystem::path& path,
              std::uint32_t sampleRate,
              std::uint16_t channels,
              std::string& error);
    bool appendInterleaved(const float* samples,
                           std::size_t frameCount,
                           std::string& error);
    bool close(std::string& error);
    bool isOpen() const noexcept;
    std::uint64_t framesWritten() const noexcept;

private:
    void writeHeader(std::uint32_t dataBytes);

    std::fstream stream_;
    std::uint32_t sampleRate_{};
    std::uint16_t channels_{};
    std::uint64_t framesWritten_{};
};

} // namespace omnistem::daw
