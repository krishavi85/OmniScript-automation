#pragma once

#include "StudioWidgets.h"
#include <memory>
#include <vector>

namespace omnistem::desktop {
class WorkerService;

struct FrequencyMask {
    juce::Rectangle<float> normalized;
    double gainDb{-12.0};
};

class FrequencyPaintCanvas final : public juce::Component {
public:
    void loadFile(const juce::File& file);
    void setGain(double gainDb);
    void clearMasks();
    const std::vector<FrequencyMask>& masks() const noexcept;
    double durationSeconds() const noexcept;
    double nyquistHz() const noexcept;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> normalizedDrag() const;
    juce::Image spectrogram;
    std::vector<FrequencyMask> painted;
    juce::Point<float> dragStart, dragEnd;
    double gain{-12.0};
    double duration{};
    double nyquist{24000.0};
    bool dragging{};
};

std::unique_ptr<juce::Component> createFrequencyPaintWorkspace(WorkerService& worker);
}
