namespace omnistem::desktop {
namespace {
int desktopLogLevelRank(const juce::String& level) {
    if (level.equalsIgnoreCase("error")) return 4;
    if (level.equalsIgnoreCase("warning")) return 3;
    if (level.equalsIgnoreCase("info")) return 2;
    return 1;
}
}

void LogStore::add(juce::String level, juce::String category, juce::String message) {
    {
        std::scoped_lock lock(mutex);
        records.push_back({juce::Time::getCurrentTime(), std::move(level),
                           std::move(category), std::move(message)});
        while (records.size() > 5000) records.pop_front();
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
    const auto minimum = minimumLevel.isEmpty() ? 0 : desktopLogLevelRank(minimumLevel);
    std::vector<LogEntry> matches;
    std::scoped_lock lock(mutex);
    for (const auto& entry : records) {
        if (desktopLogLevelRank(entry.level) < minimum) continue;
        const auto text = (entry.level + " " + entry.category + " " + entry.message).toLowerCase();
        if (needle.isEmpty() || text.contains(needle)) matches.push_back(entry);
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
} // namespace omnistem::desktop
