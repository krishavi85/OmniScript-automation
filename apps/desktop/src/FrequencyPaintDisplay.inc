void FrequencyPaintCanvas::paint(juce::Graphics& graphics) {
    const auto bounds = getLocalBounds().toFloat();
    graphics.fillAll(juce::Colours::black);
    if (spectrogram.isValid())
        graphics.drawImage(spectrogram, bounds);

    graphics.setColour(juce::Colours::cyan.withAlpha(0.35f));
    for (const auto& mask : painted) {
        const juce::Rectangle<float> region(
            bounds.getX() + mask.normalized.getX() * bounds.getWidth(),
            bounds.getY() + mask.normalized.getY() * bounds.getHeight(),
            mask.normalized.getWidth() * bounds.getWidth(),
            mask.normalized.getHeight() * bounds.getHeight());
        graphics.fillRect(region);
        graphics.drawRect(region, 1.0f);
    }

    if (dragging) {
        graphics.setColour(juce::Colours::white.withAlpha(0.4f));
        graphics.fillRect(juce::Rectangle<float>(dragStart, dragEnd));
    }
}
