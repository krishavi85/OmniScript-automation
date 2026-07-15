void FrequencyPaintCanvas::setGain(double value) {
    gain = juce::jlimit(-120.0, 24.0, value);
}

void FrequencyPaintCanvas::clearMasks() {
    painted.clear();
    repaint();
}

const std::vector<FrequencyMask>& FrequencyPaintCanvas::masks() const noexcept {
    return painted;
}

double FrequencyPaintCanvas::durationSeconds() const noexcept {
    return duration;
}

double FrequencyPaintCanvas::nyquistHz() const noexcept {
    return nyquist;
}
