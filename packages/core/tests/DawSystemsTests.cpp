#include "omnistem/core/DawSystems.h"

#include <cmath>
#include <iostream>

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
    using namespace omnistem::daw;
    std::string error;

    TempoMap tempo;
    expect(tempo.addPoint({4.0, 60.0}, error), "tempo point added");
    expect(std::abs(tempo.secondsAtBeat(4.0) - 2.0) < 1.0e-9, "first tempo segment converts to seconds");
    expect(std::abs(tempo.secondsAtBeat(8.0) - 6.0) < 1.0e-9, "second tempo segment converts to seconds");
    expect(std::abs(tempo.beatAtSeconds(6.0) - 8.0) < 1.0e-9, "seconds convert back to beats");

    AutomationLane automation;
    expect(automation.addPoint({0.0, 0.0}, error), "first automation point added");
    expect(automation.addPoint({2.0, 1.0}, error), "second automation point added");
    expect(std::abs(automation.valueAt(1.0) - 0.5) < 1.0e-9, "automation interpolates");

    CompLane comp;
    expect(comp.addTake({"take-a", "a.wav", 0.0, 4.0}, error), "first take added");
    expect(comp.addTake({"take-b", "b.wav", 0.0, 4.0}, error), "second take added");
    expect(comp.addRegion({0.0, 2.0, "take-a"}, error), "first comp region added");
    expect(comp.addRegion({2.0, 4.0, "take-b"}, error), "second comp region added");
    expect(comp.selectedTakeAt(3.0).has_value() && comp.selectedTakeAt(3.0)->id == "take-b",
           "comp lane resolves selected take");

    RoutingGraph graph;
    expect(graph.addNode({"track-a", 64}, error), "track a added");
    expect(graph.addNode({"track-b", 128}, error), "track b added");
    expect(graph.addNode({"master-a", 32}, error), "master a added");
    expect(graph.addNode({"master-b", 32}, error), "master b added");
    expect(graph.connect({"track-a", "master-a"}, error), "route a connected");
    expect(graph.connect({"track-b", "master-b"}, error), "route b connected");
    const auto latencies = graph.accumulatedLatency(error);
    expect(latencies.at("master-a") == 96, "route a latency accumulated");
    expect(latencies.at("master-b") == 160, "route b latency accumulated");
    const auto compensation = graph.outputCompensation(error);
    expect(compensation.at("master-a") == 64 && compensation.at("master-b") == 0,
           "output latency compensation calculated");
    expect(!graph.connect({"master-a", "track-a"}, error), "routing cycles rejected");

    PluginChain plugins;
    expect(plugins.insert(0, {"eq", "VST3", "example.eq", 16, false, {1, 2}}, error), "EQ inserted");
    expect(plugins.insert(1, {"limiter", "VST3", "example.limiter", 128, false, {3, 4}}, error), "limiter inserted");
    expect(plugins.totalLatencySamples() == 144, "plugin latency summed");
    plugins.find("eq")->bypassed = true;
    expect(plugins.totalLatencySamples() == 128, "bypassed plugin latency excluded");
    expect(plugins.remove("eq"), "plugin removed");

    if (failures == 0) std::cout << "All OmniStem DAW system tests passed\n";
    return failures == 0 ? 0 : 1;
}
