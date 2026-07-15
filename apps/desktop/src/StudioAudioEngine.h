#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace omnistem::desktop {

class StudioAudioEngine final : public juce::AudioSource,
                                public juce::ChangeBroadcaster {
public:
    enum class Mode { preview, comparison, stemMixer };

    struct StemMixSnapshot {
        juce::String id;
        juce::String name;
        juce::File file;
        float gainDb{};
        float pan{};
        bool muted{};
        bool solo{};
    };

    explicit StudioAudioEngine(juce::AudioFormatManager& formats);
    ~StudioAudioEngine() override;

    bool loadPreview(const juce::File& file, juce::String& error);
    bool loadComparison(const juce::File& a, const juce::File& b, juce::String& error);
    void selectComparisonB(bool useB);
    bool isComparisonBSelected() const noexcept { return comparisonB.load(); }

    bool addStem(juce::String id, juce::String name,
                 const juce::File& file, juce::String& error);
    void clearStems();
    bool setStemMix(const juce::String& id, float gainDb, float pan,
                    bool muted, bool solo);
    std::vector<StemMixSnapshot> stemMix() const;

    void setMode(Mode nextMode);
    Mode getMode() const noexcept { return mode.load(); }
    void play();
    void stop();
    void setPosition(double seconds);
    double getPosition() const;
    double getLength() const;
    bool isPlaying() const;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    struct Transport {
        std::unique_ptr<juce::AudioFormatReaderSource> reader;
        juce::AudioTransportSource source;
        juce::File file;
        double sourceSampleRate{};

        void reset();
        void prepare(int blockSize, double sampleRate);
        void release();
    };

    struct StemTrack {
        juce::String id;
        juce::String name;
        Transport transport;
        std::atomic<float> gainDb{0.0f};
        std::atomic<float> pan{0.0f};
        std::atomic_bool muted{false};
        std::atomic_bool solo{false};
    };

    std::unique_ptr<Transport> makeTransport(const juce::File& file,
                                             juce::String& error) const;
    void renderComparison(const juce::AudioSourceChannelInfo& info);
    void renderStems(const juce::AudioSourceChannelInfo& info);
    static float decibelsToGain(float gainDb);

    juce::AudioFormatManager& formatManager;
    mutable juce::SpinLock transportLock;
    Transport preview;
    Transport comparisonA;
    Transport comparisonBTransport;
    std::vector<std::unique_ptr<StemTrack>> stems;
    std::atomic<Mode> mode{Mode::preview};
    std::atomic_bool comparisonB{false};
    int preparedBlockSize{};
    double preparedSampleRate{};
    juce::AudioBuffer<float> temporaryA;
    juce::AudioBuffer<float> temporaryB;
};

} // namespace omnistem::desktop
