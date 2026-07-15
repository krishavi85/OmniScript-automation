#include "StemResultLoader.h"
#include "StudioAudioEngine.h"
#include "StudioServices.h"

namespace omnistem::desktop {

int loadNormalizedStems(const juce::var& result,
                        StudioState& state,
                        StudioAudioEngine& audio,
                        juce::StringArray& errors) {
    auto* root = result.getDynamicObject();
    if (root == nullptr)
        return 0;
    auto* stems = root->getProperty("stems").getDynamicObject();
    if (stems == nullptr)
        return 0;

    audio.clearStems();
    state.project().stems.clear();
    int loaded = 0;
    const auto& values = stems->getProperties();
    for (int index = 0; index < values.size(); ++index) {
        const auto name = values.getName(index).toString();
        const juce::File file(values.getValueAt(index).toString());
        if (!file.existsAsFile()) {
            errors.add(name + ": file not found");
            continue;
        }
        juce::String error;
        if (!audio.addStem(name, name, file, error)) {
            errors.add(name + ": " + error);
            continue;
        }
        omnistem::Stem stem;
        stem.id = name.toStdString();
        stem.name = name.toStdString();
        stem.audioPath = file.getFullPathName().toStdString();
        state.project().stems.push_back(std::move(stem));
        ++loaded;
    }
    if (loaded > 0)
        audio.setMode(StudioAudioEngine::Mode::stemMixer);
    return loaded;
}

} // namespace omnistem::desktop
