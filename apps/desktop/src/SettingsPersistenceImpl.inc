namespace omnistem::desktop {

void StudioState::saveSettings() {
    auto* file = settings.getUserSettings();
    if (file == nullptr) return;
    file->setValue("apiUrl", apiUrl);
    file->setValue("outputDirectory", outputDirectory.getFullPathName());
    file->saveIfNeeded();
}

void StudioState::loadSettings() {
    const auto fallback = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              .getChildFile("OmniStem Studio")
                              .getChildFile("Outputs");
    auto* file = settings.getUserSettings();
    if (file == nullptr) {
        outputDirectory = fallback;
        outputDirectory.createDirectory();
        return;
    }
    apiUrl = file->getValue("apiUrl", "http://127.0.0.1:8765");
    outputDirectory = juce::File(file->getValue(
        "outputDirectory", fallback.getFullPathName()));
    outputDirectory.createDirectory();
}

} // namespace omnistem::desktop
