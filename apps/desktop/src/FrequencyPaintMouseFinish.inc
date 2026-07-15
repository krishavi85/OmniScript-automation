void FrequencyPaintCanvas::mouseUp(const juce::MouseEvent& event) {
    dragEnd = event.position;
    dragging = false;
    auto region = juce::Rectangle<float>(dragStart, dragEnd);
    if (region.getWidth() > 2.0f && region.getHeight() > 2.0f)
        painted.push_back({region, gain});
    repaint();
}
