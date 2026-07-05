namespace omnistem::desktop {

StudioState::StudioState(LogStore& logs) : logStore(logs) {
    juce::PropertiesFile::Options options;
    options.applicationName = "OmniStemStudio";
    options.filenameSuffix = ".settings";
    options.folderName = "OmniStemStudio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    settings.setStorageParameters(options);
    currentProject.id = juce::Uuid().toString().toStdString();
    currentProject.name = "Untitled OmniStem Project";
    loadSettings();
}

void StudioState::setCurrentAudio(const juce::File& file) {
    if (file == currentAudio) return;
    currentAudio = file;
    if (file.existsAsFile()) {
        currentProject.name = file.getFileNameWithoutExtension().toStdString();
        logStore.add("info", "project", "Loaded source audio: " + file.getFullPathName());
    }
    sendChangeMessage();
}

void StudioState::setOutputDirectory(const juce::File& directory) {
    if (directory == outputDirectory) return;
    outputDirectory = directory;
    if (!outputDirectory.exists()) outputDirectory.createDirectory();
    saveSettings();
    sendChangeMessage();
}

void StudioState::setApiUrl(juce::String url) {
    url = url.trim();
    while (url.endsWithChar('/')) url = url.dropLastCharacters(1);
    if (url.isEmpty() || url == apiUrl) return;
    apiUrl = std::move(url);
    saveSettings();
    logStore.add("info", "settings", "Worker API set to " + apiUrl);
    sendChangeMessage();
}

} // namespace omnistem::desktop
