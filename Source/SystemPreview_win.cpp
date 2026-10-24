#include "SystemPreview.h"
#include <atomic>
#include <cmath>
#include <mutex>

namespace flowalign
{
struct SystemPreview::Impl final : juce::AudioIODeviceCallback
{
    // Only lifecycle calls take this mutex. The audio callback never locks or allocates.
    mutable std::recursive_mutex mutex;
    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::AudioBuffer<float> audio;
    double sourceRate = 0.0, deviceRate = 0.0, cursor = 0.0;
    std::atomic<bool> playing { false }, looping { false };
    std::atomic<double> seconds { 0.0 }, requestedSeek { 0.0 };
    std::atomic<unsigned> seekRevision { 0 };
    unsigned appliedSeek = 0;
    std::atomic<float> level { 0.0f };

    void audioDeviceAboutToStart(juce::AudioIODevice* output) override
    { deviceRate = output->getCurrentSampleRate(); }
    void audioDeviceStopped() override { playing.store(false); level.store(0.0f); }
    void audioDeviceError(const juce::String&) override { playing.store(false); level.store(0.0f); }
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output,
        int channels, int count, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < channels; ++c)
            if (output[c] != nullptr) juce::FloatVectorOperations::clear(output[c], count);
        if (!playing.load() || deviceRate <= 0.0 || audio.getNumSamples() == 0) return;
        const auto revision = seekRevision.load();
        if (appliedSeek != revision)
        { cursor = requestedSeek.load() * sourceRate; appliedSeek = revision; }
        const auto length = audio.getNumSamples();
        const double step = sourceRate / deviceRate;
        float maximum = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            if (cursor >= length)
            {
                if (looping.load()) cursor = std::fmod(cursor, static_cast<double>(length));
                else { cursor = length; playing.store(false); break; }
            }
            const int a = juce::jlimit(0, length - 1, static_cast<int>(cursor));
            const int b = juce::jmin(a + 1, length - 1);
            const float fraction = static_cast<float>(cursor - a);
            for (int c = 0; c < channels; ++c)
            {
                if (output[c] == nullptr) continue;
                // Mono is duplicated; stereo remains stereo on the first two speakers.
                if (c >= 2) continue;
                const auto* input = audio.getReadPointer(juce::jmin(c, audio.getNumChannels() - 1));
                const float value = input[a] + fraction * (input[b] - input[a]);
                output[c][i] = value;
                maximum = juce::jmax(maximum, std::abs(value));
            }
            cursor += step;
        }
        if (seekRevision.load() == revision) seconds.store(cursor / sourceRate);
        level.store(maximum);
    }
};

SystemPreview::SystemPreview() : impl(std::make_unique<Impl>()) {}
SystemPreview::~SystemPreview() { stop(); }

bool SystemPreview::start(const juce::MemoryBlock& wave, double startSeconds, bool loop, juce::String& error)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    stop();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::MemoryInputStream(wave, false), true));
    if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > 23040000
        || reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate <= 0)
    { error = juce::String::fromUTF8(u8"Нет подходящего аудио для прослушивания"); return false; }
    impl->audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&impl->audio, 0, impl->audio.getNumSamples(), 0, true, true))
    { error = juce::String::fromUTF8(u8"Не удалось прочитать аудио для прослушивания"); return false; }
    impl->sourceRate = reader->sampleRate;
    impl->cursor = juce::jlimit(0.0, static_cast<double>(impl->audio.getNumSamples()), startSeconds * reader->sampleRate);
    impl->seconds.store(impl->cursor / impl->sourceRate);
    impl->seekRevision.store(0); impl->appliedSeek = 0; impl->looping.store(loop);
    impl->type.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    if (impl->type)
    {
        impl->type->scanForDevices();
        const auto names = impl->type->getDeviceNames(false);
        const int index = impl->type->getDefaultDeviceIndex(false);
        if (juce::isPositiveAndBelow(index, names.size()))
            impl->device.reset(impl->type->createDevice(names[index], {}));
    }
    if (!impl->device)
    { error = juce::String::fromUTF8(u8"Windows не нашла системный аудиовыход. Проверьте выход звука"); stop(); return false; }
    juce::BigInteger noInputs, outputs;
    outputs.setRange(0, juce::jmin(2, impl->device->getOutputChannelNames().size()), true);
    if (outputs.isZero())
    { error = juce::String::fromUTF8(u8"У системного устройства нет выходных каналов"); stop(); return false; }
    // Use the Windows shared mix rate. Do not change the DAW's ASIO device or rate.
    error = impl->device->open(noInputs, outputs, 0.0, 0);
    if (error.isNotEmpty())
    { error = juce::String::fromUTF8(u8"Windows не открыла системный аудиовыход: ") + error; stop(); return false; }
    impl->playing.store(true);
    impl->device->start(impl.get());
    if (!impl->device->isPlaying())
    { error = juce::String::fromUTF8(u8"Windows не запустила аудиовыход. Проверьте, не занят ли он в эксклюзивном режиме"); stop(); return false; }
    return true;
}
void SystemPreview::stop()
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    impl->playing.store(false);
    if (impl->device) { impl->device->stop(); impl->device->close(); }
    impl->device.reset(); impl->type.reset();
    impl->audio.setSize(0, 0); impl->level.store(0.0f);
}
bool SystemPreview::isPlaying() const { return impl->playing.load(); }
double SystemPreview::position() const { return impl->seconds.load(); }
void SystemPreview::seek(double positionSeconds)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    const double duration = impl->sourceRate > 0 ? impl->audio.getNumSamples() / impl->sourceRate : 0.0;
    const double target = juce::jlimit(0.0, duration, positionSeconds);
    impl->requestedSeek.store(target); impl->seekRevision.fetch_add(1); impl->seconds.store(target);
}
void SystemPreview::setLooping(bool loop) { impl->looping.store(loop); }
float SystemPreview::peak() const { return impl->playing.load() ? impl->level.load() : 0.0f; }
}
