#pragma once

#include <JuceHeader.h>
#include <functional>

namespace omnistem::desktop {
using ResultHook = std::function<void(const juce::var&)>;
void setResultHook(ResultHook hook);
void clearResultHook();
void notifyResultHook(const juce::var& value);
}
