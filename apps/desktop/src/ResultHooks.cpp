#include "ResultHooks.h"

namespace omnistem::desktop {
namespace {
ResultHook resultHook;
}

void setResultHook(ResultHook hook) {
    resultHook = std::move(hook);
}

void clearResultHook() {
    resultHook = {};
}

void notifyResultHook(const juce::var& value) {
    if (resultHook)
        resultHook(value);
}
}
