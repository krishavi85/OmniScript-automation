#include "StudioAudioEngine.h"

#include <algorithm>
#include <cmath>

namespace omnistem::desktop {

StudioAudioEngine::StudioAudioEngine(juce::AudioFormatManager& formats)
    : formatManager(formats) {}

StudioAudioEngine::~StudioAudioEngine() {
    stop();
    releaseResources();
    const juce::SpinLock::ScopedLockType lock(transportLock);
    preview.reset();
    comparisonA.reset();
    comparisonBTransport.reset();
    stems.clear();
}

void StudioAudioEngine::Transport::reset() {
    source.stop();
    source.setSource(nullptr);
    reader.reset();
    file = {};
    sourceSampleRate = 0.0;
}

void StudioAudioEngine::Transport::prepare(int blockSize, double sampleRate) {
    source.prepareToPlay(blockSize, sampleRate);
}

void StudioAudioEngine::Transport::release() {
    source.releaseResources();
}

std::unique_ptr<StudioAudioEngine::Transport>
StudioAudioEngine::makeTransport(const juce::File& file, juce::String& error) const {
    if (!file.existsAsFile()) {
        error = "Audio file does not exist: " + file.getFullPathName();
        return {};
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return {};
    }

    auto transport = std::make_unique<Transport>();
    transport->file = file;
    transport->sourceSampleRate = reader->sampleRate;
    transport->reader = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    transport->source.setSource(transport->reader.get(), 0, nullptr, transport->sourceSampleRate);
    return transport;
}

bool StudioAudioEngine::loadPreview(const juce::File& file, juce::String& error) {
    auto next = makeTransport(file, error);
    if (!next) return false;

    const juce::SpinLock::ScopedLockType lock(transportLock);
    preview.reset();
    preview.file = next->file;
    preview.sourceSampleRate = next->sourceSampleRate;
    preview.reader = std::move(next->reader);
    preview.source.setSource(preview.reader.get(), 0, nullptr, preview.sourceSampleRate);
    if (preparedSampleRate > 0.0)
        preview.prepare(preparedBlockSize, preparedSampleRate);
    mode.store(Mode::preview);
    sendChangeMessage();
    return true;
}

bool StudioAudioEngine::loadComparison(const juce::File& a,
                                       const juce::File& b,
                                       juce::String& error) {
    auto nextA = makeTransport(a, error);
    if (!nextA) return false;
    auto nextB = makeTransport(b, error);
    if (!nextB) return false;

    const juce::SpinLock::ScopedLockType lock(transportLock);
    comparisonA.reset();
    comparisonBTransport.reset();

    comparisonA.file = nextA->file;
    comparisonA.sourceSampleRate = nextA->sourceSampleRate;
    comparisonA.reader = std::move(nextA->reader);
    comparisonA.source.setSource(comparisonA.reader.get(), 0, nullptr,
                                 comparisonA.sourceSampleRate);

    comparisonBTransport.file = nextB->file;
    comparisonBTransport.sourceSampleRate = nextB->sourceSampleRate;
    comparisonBTransport.reader = std::move(nextB->reader);
    comparisonBTransport.source.setSource(comparisonBTransport.reader.get(), 0, nullptr,
                                          comparisonBTransport.sourceSampleRate);

    if (preparedSampleRate > 0.0) {
        comparisonA.prepare(preparedBlockSize, preparedSampleRate);
        comparisonBTransport.prepare(preparedBlockSize, preparedSampleRate);
    }
    comparisonB.store(false);
    mode.store(Mode::comparison);
    sendChangeMessage();
    return true;
}

void StudioAudioEngine::selectComparisonB(bool useB) {
    comparisonB.store(useB);
    sendChangeMessage();
}

bool StudioAudioEngine::addStem(juce::String id,
                                juce::String name,
                                const juce::File& file,
                                juce::String& error) {
    auto next = makeTransport(file, error);
    if (!next) return false;

    auto track = std::make_unique<StemTrack>();
    track->id = std::move(id);
    track->name = std::move(name);
    track->transport.file = next->file;
    track->transport.sourceSampleRate = next->sourceSampleRate;
    track->transport.reader = std::move(next->reader);
    track->transport.source.setSource(track->transport.reader.get(), 0, nullptr,
                                      track->transport.sourceSampleRate);

    {
        const juce::SpinLock::ScopedLockType lock(transportLock);
        const auto duplicate = std::find_if(stems.begin(), stems.end(), [&](const auto& item) {
            return item->id == track->id;
        });
        if (duplicate != stems.end())
            stems.erase(duplicate);
        if (preparedSampleRate > 0.0)
            track->transport.prepare(preparedBlockSize, preparedSampleRate);
        stems.push_back(std::move(track));
        mode.store(Mode::stemMixer);
    }
    sendChangeMessage();
    return true;
}

void StudioAudioEngine::clearStems() {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    for (auto& track : stems)
        track->transport.reset();
    stems.clear();
    sendChangeMessage();
}

bool StudioAudioEngine::setStemMix(const juce::String& id,
                                   float gainDb,
                                   float pan,
                                   bool muted,
                                   bool solo) {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    const auto found = std::find_if(stems.begin(), stems.end(), [&](const auto& track) {
        return track->id == id;
    });
    if (found == stems.end()) return false;
    (*found)->gainDb.store(juce::jlimit(-60.0f, 12.0f, gainDb));
    (*found)->pan.store(juce::jlimit(-1.0f, 1.0f, pan));
    (*found)->muted.store(muted);
    (*found)->solo.store(solo);
    sendChangeMessage();
    return true;
}

std::vector<StudioAudioEngine::StemMixSnapshot> StudioAudioEngine::stemMix() const {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    std::vector<StemMixSnapshot> result;
    result.reserve(stems.size());
    for (const auto& track : stems) {
        result.push_back({track->id, track->name, track->transport.file,
                          track->gainDb.load(), track->pan.load(),
                          track->muted.load(), track->solo.load()});
    }
    return result;
}

void StudioAudioEngine::setMode(Mode nextMode) {
    stop();
    mode.store(nextMode);
    sendChangeMessage();
}

void StudioAudioEngine::play() {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    switch (mode.load()) {
        case Mode::preview:
            if (preview.reader) preview.source.start();
            break;
        case Mode::comparison: {
            const auto position = comparisonB.load()
                                    ? comparisonBTransport.source.getCurrentPosition()
                                    : comparisonA.source.getCurrentPosition();
            comparisonA.source.setPosition(position);
            comparisonBTransport.source.setPosition(position);
            if (comparisonA.reader && comparisonBTransport.reader) {
                comparisonA.source.start();
                comparisonBTransport.source.start();
            }
            break;
        }
        case Mode::stemMixer: {
            const auto position = stems.empty() ? 0.0 : stems.front()->transport.source.getCurrentPosition();
            for (auto& track : stems) {
                track->transport.source.setPosition(position);
                track->transport.source.start();
            }
            break;
        }
    }
    sendChangeMessage();
}

void StudioAudioEngine::stop() {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    preview.source.stop();
    comparisonA.source.stop();
    comparisonBTransport.source.stop();
    for (auto& track : stems)
        track->transport.source.stop();
    sendChangeMessage();
}

void StudioAudioEngine::setPosition(double seconds) {
    seconds = std::max(0.0, seconds);
    const juce::SpinLock::ScopedLockType lock(transportLock);
    preview.source.setPosition(seconds);
    comparisonA.source.setPosition(seconds);
    comparisonBTransport.source.setPosition(seconds);
    for (auto& track : stems)
        track->transport.source.setPosition(seconds);
    sendChangeMessage();
}

double StudioAudioEngine::getPosition() const {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    switch (mode.load()) {
        case Mode::preview: return preview.source.getCurrentPosition();
        case Mode::comparison:
            return comparisonB.load() ? comparisonBTransport.source.getCurrentPosition()
                                      : comparisonA.source.getCurrentPosition();
        case Mode::stemMixer:
            return stems.empty() ? 0.0 : stems.front()->transport.source.getCurrentPosition();
    }
    return 0.0;
}

double StudioAudioEngine::getLength() const {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    switch (mode.load()) {
        case Mode::preview: return preview.source.getLengthInSeconds();
        case Mode::comparison:
            return std::min(comparisonA.source.getLengthInSeconds(),
                            comparisonBTransport.source.getLengthInSeconds());
        case Mode::stemMixer: {
            double length = 0.0;
            for (const auto& track : stems)
                length = std::max(length, track->transport.source.getLengthInSeconds());
            return length;
        }
    }
    return 0.0;
}

bool StudioAudioEngine::isPlaying() const {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    switch (mode.load()) {
        case Mode::preview: return preview.source.isPlaying();
        case Mode::comparison:
            return comparisonA.source.isPlaying() || comparisonBTransport.source.isPlaying();
        case Mode::stemMixer:
            return std::any_of(stems.begin(), stems.end(), [](const auto& track) {
                return track->transport.source.isPlaying();
            });
    }
    return false;
}

void StudioAudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    preparedBlockSize = samplesPerBlockExpected;
    preparedSampleRate = sampleRate;
    preview.prepare(samplesPerBlockExpected, sampleRate);
    comparisonA.prepare(samplesPerBlockExpected, sampleRate);
    comparisonBTransport.prepare(samplesPerBlockExpected, sampleRate);
    for (auto& track : stems)
        track->transport.prepare(samplesPerBlockExpected, sampleRate);
    temporaryA.setSize(2, samplesPerBlockExpected, false, false, true);
    temporaryB.setSize(2, samplesPerBlockExpected, false, false, true);
}

void StudioAudioEngine::releaseResources() {
    const juce::SpinLock::ScopedLockType lock(transportLock);
    preview.release();
    comparisonA.release();
    comparisonBTransport.release();
    for (auto& track : stems)
        track->transport.release();
    preparedBlockSize = 0;
    preparedSampleRate = 0.0;
}

void StudioAudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    bufferToFill.clearActiveBufferRegion();
    const juce::SpinLock::ScopedTryLockType lock(transportLock);
    if (!lock.isLocked()) return;

    switch (mode.load()) {
        case Mode::preview:
            if (preview.reader) preview.source.getNextAudioBlock(bufferToFill);
            break;
        case Mode::comparison:
            renderComparison(bufferToFill);
            break;
        case Mode::stemMixer:
            renderStems(bufferToFill);
            break;
    }
}

void StudioAudioEngine::renderComparison(const juce::AudioSourceChannelInfo& info) {
    if (!comparisonA.reader || !comparisonBTransport.reader) return;
    const auto channels = info.buffer->getNumChannels();
    temporaryA.setSize(channels, info.numSamples, false, false, true);
    temporaryB.setSize(channels, info.numSamples, false, false, true);
    temporaryA.clear();
    temporaryB.clear();
    juce::AudioSourceChannelInfo aInfo(&temporaryA, 0, info.numSamples);
    juce::AudioSourceChannelInfo bInfo(&temporaryB, 0, info.numSamples);
    comparisonA.source.getNextAudioBlock(aInfo);
    comparisonBTransport.source.getNextAudioBlock(bInfo);
    const auto& selected = comparisonB.load() ? temporaryB : temporaryA;
    for (int channel = 0; channel < channels; ++channel)
        info.buffer->copyFrom(channel, info.startSample, selected, channel, 0, info.numSamples);
}

void StudioAudioEngine::renderStems(const juce::AudioSourceChannelInfo& info) {
    if (stems.empty()) return;
    const auto anySolo = std::any_of(stems.begin(), stems.end(), [](const auto& track) {
        return track->solo.load();
    });
    const auto channels = info.buffer->getNumChannels();
    temporaryA.setSize(channels, info.numSamples, false, false, true);

    for (auto& track : stems) {
        temporaryA.clear();
        juce::AudioSourceChannelInfo trackInfo(&temporaryA, 0, info.numSamples);
        track->transport.source.getNextAudioBlock(trackInfo);
        const auto audible = !track->muted.load() && (!anySolo || track->solo.load());
        if (!audible) continue;

        const auto gain = decibelsToGain(track->gainDb.load());
        const auto pan = juce::jlimit(-1.0f, 1.0f, track->pan.load());
        const auto leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const auto rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

        if (channels == 1) {
            info.buffer->addFrom(0, info.startSample, temporaryA, 0, 0, info.numSamples, gain);
        } else {
            info.buffer->addFrom(0, info.startSample, temporaryA, 0, 0, info.numSamples, leftGain);
            const auto sourceRight = temporaryA.getNumChannels() > 1 ? 1 : 0;
            info.buffer->addFrom(1, info.startSample, temporaryA, sourceRight, 0,
                                 info.numSamples, rightGain);
            for (int channel = 2; channel < channels; ++channel)
                info.buffer->addFrom(channel, info.startSample, temporaryA,
                                     std::min(channel, temporaryA.getNumChannels() - 1),
                                     0, info.numSamples, gain);
        }
    }
}

float StudioAudioEngine::decibelsToGain(float gainDb) {
    return gainDb <= -60.0f ? 0.0f : std::pow(10.0f, gainDb / 20.0f);
}

} // namespace omnistem::desktop
