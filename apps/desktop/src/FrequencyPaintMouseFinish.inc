void FrequencyPaintCanvas::mouseUp(const juce::MouseEvent& event) {
    dragEnd = event.position;
    dragging = false;
    const auto region = normalizedDrag();
    if (region.getWidth() > 0.002f && region.getHeight() > 0.002f)
        painted.push_back({region, gain});
    repaint();
}
