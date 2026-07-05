void FrequencyPaintCanvas::mouseDown(const juce::MouseEvent& event) {
    dragging = true;
    dragStart = event.position;
    dragEnd = event.position;
    repaint();
}

void FrequencyPaintCanvas::mouseDrag(const juce::MouseEvent& event) {
    dragEnd = event.position;
    repaint();
}
