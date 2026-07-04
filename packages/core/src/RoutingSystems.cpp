#include "omnistem/core/DawSystems.h"

#include <algorithm>
#include <queue>
#include <set>

namespace omnistem::daw {

bool RoutingGraph::addNode(RouteNode node, std::string& error) {
    if (node.id.empty()) {
        error = "Route node ID must not be empty";
        return false;
    }
    if (std::any_of(nodes_.begin(), nodes_.end(), [&](const RouteNode& existing) { return existing.id == node.id; })) {
        error = "Duplicate route node ID";
        return false;
    }
    nodes_.push_back(std::move(node));
    return true;
}

bool RoutingGraph::connect(RouteEdge edge, std::string& error) {
    const auto exists = [&](const std::string& id) {
        return std::any_of(nodes_.begin(), nodes_.end(), [&](const RouteNode& node) { return node.id == id; });
    };
    if (!exists(edge.source) || !exists(edge.destination) || edge.source == edge.destination) {
        error = "Route edge requires two distinct existing nodes";
        return false;
    }
    if (std::any_of(edges_.begin(), edges_.end(), [&](const RouteEdge& existing) {
        return existing.source == edge.source && existing.destination == edge.destination;
    })) {
        error = "Duplicate route edge";
        return false;
    }
    edges_.push_back(edge);
    std::string cycleError;
    if (topologicalOrder(cycleError).empty() && !nodes_.empty()) {
        edges_.pop_back();
        error = "Route edge creates a cycle";
        return false;
    }
    return true;
}

std::vector<std::string> RoutingGraph::topologicalOrder(std::string& error) const {
    std::map<std::string, std::size_t> indegree;
    std::map<std::string, std::vector<std::string>> outgoing;
    for (const auto& node : nodes_) indegree[node.id] = 0;
    for (const auto& edge : edges_) {
        ++indegree[edge.destination];
        outgoing[edge.source].push_back(edge.destination);
    }
    std::queue<std::string> ready;
    for (const auto& [id, degree] : indegree)
        if (degree == 0) ready.push(id);
    std::vector<std::string> order;
    while (!ready.empty()) {
        auto id = ready.front();
        ready.pop();
        order.push_back(id);
        for (const auto& destination : outgoing[id])
            if (--indegree[destination] == 0) ready.push(destination);
    }
    if (order.size() != nodes_.size()) {
        error = "Routing graph contains a cycle";
        return {};
    }
    return order;
}

std::map<std::string, std::uint64_t> RoutingGraph::accumulatedLatency(std::string& error) const {
    const auto order = topologicalOrder(error);
    if (order.empty() && !nodes_.empty()) return {};
    std::map<std::string, std::uint64_t> latency;
    std::map<std::string, std::uint32_t> intrinsic;
    std::map<std::string, std::vector<std::string>> incoming;
    for (const auto& node : nodes_) intrinsic[node.id] = node.intrinsicLatencySamples;
    for (const auto& edge : edges_) incoming[edge.destination].push_back(edge.source);
    for (const auto& id : order) {
        std::uint64_t longestInput = 0;
        for (const auto& source : incoming[id]) longestInput = std::max(longestInput, latency[source]);
        latency[id] = longestInput + intrinsic[id];
    }
    return latency;
}

std::map<std::string, std::uint64_t> RoutingGraph::outputCompensation(std::string& error) const {
    const auto latency = accumulatedLatency(error);
    if (latency.empty() && !nodes_.empty()) return {};
    std::set<std::string> hasOutgoing;
    for (const auto& edge : edges_) hasOutgoing.insert(edge.source);
    std::uint64_t maximum = 0;
    for (const auto& node : nodes_)
        if (!hasOutgoing.contains(node.id)) maximum = std::max(maximum, latency.at(node.id));
    std::map<std::string, std::uint64_t> result;
    for (const auto& node : nodes_)
        if (!hasOutgoing.contains(node.id)) result[node.id] = maximum - latency.at(node.id);
    return result;
}

const std::vector<RouteNode>& RoutingGraph::nodes() const noexcept { return nodes_; }
const std::vector<RouteEdge>& RoutingGraph::edges() const noexcept { return edges_; }

bool PluginChain::insert(std::size_t index, PluginState plugin, std::string& error) {
    if (plugin.instanceId.empty() || plugin.identifier.empty()) {
        error = "Plugin instance and identifier must not be empty";
        return false;
    }
    if (find(plugin.instanceId)) {
        error = "Duplicate plugin instance ID";
        return false;
    }
    index = std::min(index, plugins_.size());
    plugins_.insert(plugins_.begin() + static_cast<std::ptrdiff_t>(index), std::move(plugin));
    return true;
}

bool PluginChain::remove(const std::string& instanceId) {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const PluginState& plugin) {
        return plugin.instanceId == instanceId;
    });
    if (it == plugins_.end()) return false;
    plugins_.erase(it);
    return true;
}

PluginState* PluginChain::find(const std::string& instanceId) {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const PluginState& plugin) {
        return plugin.instanceId == instanceId;
    });
    return it == plugins_.end() ? nullptr : &*it;
}

const PluginState* PluginChain::find(const std::string& instanceId) const {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const PluginState& plugin) {
        return plugin.instanceId == instanceId;
    });
    return it == plugins_.end() ? nullptr : &*it;
}

std::uint64_t PluginChain::totalLatencySamples() const noexcept {
    std::uint64_t total = 0;
    for (const auto& plugin : plugins_)
        if (!plugin.bypassed) total += plugin.latencySamples;
    return total;
}

const std::vector<PluginState>& PluginChain::plugins() const noexcept { return plugins_; }

} // namespace omnistem::daw
