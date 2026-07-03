#include "omnistem/core/DawSystems.h"

#include <algorithm>
#include <cmath>

namespace omnistem::daw {
namespace {

bool finiteNonNegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

TempoMap::TempoMap() {
    points_.push_back({0.0, 120.0});
}

bool TempoMap::addPoint(TempoPoint point, std::string& error) {
    if (!finiteNonNegative(point.beat) || !std::isfinite(point.bpm) || point.bpm <= 0.0) {
        error = "Tempo point requires a non-negative beat and positive finite BPM";
        return false;
    }
    const auto it = std::lower_bound(points_.begin(), points_.end(), point.beat,
        [](const TempoPoint& existing, double beat) { return existing.beat < beat; });
    if (it != points_.end() && std::abs(it->beat - point.beat) < 1.0e-9)
        *it = point;
    else
        points_.insert(it, point);
    return true;
}

double TempoMap::secondsAtBeat(double beat) const {
    if (!std::isfinite(beat) || beat <= 0.0) return 0.0;
    double seconds = 0.0;
    for (std::size_t index = 0; index < points_.size(); ++index) {
        const auto& current = points_[index];
        const double nextBeat = index + 1 < points_.size() ? points_[index + 1].beat : beat;
        const double segmentEnd = std::min(beat, nextBeat);
        if (segmentEnd > current.beat)
            seconds += (segmentEnd - current.beat) * 60.0 / current.bpm;
        if (beat <= nextBeat) break;
    }
    return seconds;
}

double TempoMap::beatAtSeconds(double seconds) const {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0.0;
    double elapsed = 0.0;
    for (std::size_t index = 0; index < points_.size(); ++index) {
        const auto& current = points_[index];
        if (index + 1 == points_.size())
            return current.beat + (seconds - elapsed) * current.bpm / 60.0;
        const auto& next = points_[index + 1];
        const double segmentSeconds = (next.beat - current.beat) * 60.0 / current.bpm;
        if (seconds <= elapsed + segmentSeconds)
            return current.beat + (seconds - elapsed) * current.bpm / 60.0;
        elapsed += segmentSeconds;
    }
    return 0.0;
}

const std::vector<TempoPoint>& TempoMap::points() const noexcept { return points_; }

bool AutomationLane::addPoint(AutomationPoint point, std::string& error) {
    if (!finiteNonNegative(point.timeSeconds) || !std::isfinite(point.value)) {
        error = "Automation points require finite time and value";
        return false;
    }
    const auto it = std::lower_bound(points_.begin(), points_.end(), point.timeSeconds,
        [](const AutomationPoint& existing, double time) { return existing.timeSeconds < time; });
    if (it != points_.end() && std::abs(it->timeSeconds - point.timeSeconds) < 1.0e-9)
        *it = point;
    else
        points_.insert(it, point);
    return true;
}

double AutomationLane::valueAt(double timeSeconds, double fallback) const {
    if (points_.empty() || !std::isfinite(timeSeconds)) return fallback;
    if (timeSeconds <= points_.front().timeSeconds) return points_.front().value;
    if (timeSeconds >= points_.back().timeSeconds) return points_.back().value;
    const auto upper = std::upper_bound(points_.begin(), points_.end(), timeSeconds,
        [](double time, const AutomationPoint& point) { return time < point.timeSeconds; });
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const double width = right.timeSeconds - left.timeSeconds;
    if (width <= 0.0) return right.value;
    const double t = (timeSeconds - left.timeSeconds) / width;
    return left.value + (right.value - left.value) * t;
}

const std::vector<AutomationPoint>& AutomationLane::points() const noexcept { return points_; }

bool CompLane::addTake(Take take, std::string& error) {
    if (take.id.empty() || !finiteNonNegative(take.sourceOffsetSeconds) ||
        !std::isfinite(take.lengthSeconds) || take.lengthSeconds <= 0.0) {
        error = "Take requires an ID, non-negative offset, and positive length";
        return false;
    }
    if (std::any_of(takes_.begin(), takes_.end(), [&](const Take& existing) { return existing.id == take.id; })) {
        error = "Duplicate take ID";
        return false;
    }
    takes_.push_back(std::move(take));
    return true;
}

bool CompLane::addRegion(CompRegion region, std::string& error) {
    if (!finiteNonNegative(region.startSeconds) || !std::isfinite(region.endSeconds) ||
        region.endSeconds <= region.startSeconds) {
        error = "Comp region requires a valid positive range";
        return false;
    }
    if (std::none_of(takes_.begin(), takes_.end(), [&](const Take& take) { return take.id == region.takeId; })) {
        error = "Comp region references an unknown take";
        return false;
    }
    if (std::any_of(regions_.begin(), regions_.end(), [&](const CompRegion& existing) {
        return region.startSeconds < existing.endSeconds && region.endSeconds > existing.startSeconds;
    })) {
        error = "Comp regions may not overlap";
        return false;
    }
    regions_.push_back(std::move(region));
    std::sort(regions_.begin(), regions_.end(), [](const CompRegion& a, const CompRegion& b) {
        return a.startSeconds < b.startSeconds;
    });
    return true;
}

std::optional<Take> CompLane::selectedTakeAt(double timeSeconds) const {
    const auto region = std::find_if(regions_.begin(), regions_.end(), [&](const CompRegion& item) {
        return timeSeconds >= item.startSeconds && timeSeconds < item.endSeconds;
    });
    if (region == regions_.end()) return std::nullopt;
    const auto take = std::find_if(takes_.begin(), takes_.end(), [&](const Take& item) {
        return item.id == region->takeId;
    });
    return take == takes_.end() ? std::nullopt : std::optional<Take>(*take);
}

const std::vector<Take>& CompLane::takes() const noexcept { return takes_; }
const std::vector<CompRegion>& CompLane::regions() const noexcept { return regions_; }

} // namespace omnistem::daw
