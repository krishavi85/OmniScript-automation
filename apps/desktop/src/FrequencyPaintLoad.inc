void FrequencyPaintCanvas::loadFile(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        spectrogram = {};
        duration = 0.0;
        repaint();
        return;
    }
    duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    nyquist = reader->sampleRate * 0.5;
    constexpr int width = 1024;
    constexpr int height = 512;
    constexpr int order = 10;
    constexpr int fftSize = 1 << order;
    spectrogram = juce::Image(juce::Image::RGB, width, height, true);
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(fftSize,
        juce::dsp::WindowingFunction<float>::hann, true);
    juce::AudioBuffer<float> buffer(1, fftSize);
    std::vector<float> data(static_cast<std::size_t>(fftSize * 2));
    const auto available = juce::jmax<juce::int64>(1, reader->lengthInSamples - fftSize);
    for (int x = 0; x < width; ++x) {
        const auto position = static_cast<juce::int64>(available * x / juce::jmax(1, width - 1));
        buffer.clear();
        reader->read(&buffer, 0, fftSize, position, true, false);
        std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + fftSize, data.begin());
        std::fill(data.begin() + fftSize, data.end(), 0.0f);
        window.multiplyWithWindowingTable(data.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(data.data());
        for (int y = 0; y < height; ++y) {
            const auto bin = juce::jlimit(0, fftSize / 2,
                static_cast<int>((1.0 - y / static_cast<double>(height - 1)) * (fftSize / 2)));
            const auto db = juce::Decibels::gainToDecibels(
                data[static_cast<std::size_t>(bin)] / fftSize, -100.0f);
            const auto level = juce::jlimit(0.0f, 1.0f, (db + 100.0f) / 100.0f);
            spectrogram.setPixelAt(x, y,
                juce::Colour::fromHSV(0.68f - level * 0.68f, 0.9f, level, 1.0f));
        }
    }
    painted.clear();
    repaint();
}
