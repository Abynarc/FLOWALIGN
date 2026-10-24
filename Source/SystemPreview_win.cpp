#include "SystemPreview.h"
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <avrt.h>
#include <atomic>
#include <cmath>
#include <future>
#include <mutex>
#include <thread>

namespace flowalign
{
struct SystemPreview::Impl
{
    // All COM objects live and die on the output thread. A decoder/host worker may
    // stop audition safely, without destroying message-thread windows or devices.
    mutable std::recursive_mutex mutex;
    std::thread worker;
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    juce::AudioBuffer<float> audio;
    double rate = 0.0;
    std::atomic<bool> playing { false }, looping { false };
    std::atomic<double> seconds { 0.0 }, requestedSeek { 0.0 };
    std::atomic<unsigned> seekRevision { 0 };
    std::atomic<float> level { 0.0f };
    ~Impl() { if (stopEvent != nullptr) CloseHandle(stopEvent); }

    void run(std::promise<juce::String> ready)
    {
        bool notified = false;
        auto notify = [&](const juce::String& error)
        { if (!notified) { ready.set_value(error); notified = true; } };
        try
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            struct Apartment
            {
                bool initialized;
                ~Apartment() { if (initialized) CoUninitialize(); }
            } apartment { SUCCEEDED(initialized) };
            auto failure = [&](HRESULT result)
            {
                notify(juce::String::fromUTF8(u8"Windows не открыла системный аудиовыход (WASAPI 0x")
                    + juce::String::toHexString(static_cast<int>(result))
                    + juce::String::fromUTF8(u8"). Проверьте выход звука и эксклюзивный режим ASIO"));
            };
            if (FAILED(initialized)) { failure(initialized); return; }
            using Microsoft::WRL::ComPtr;
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDevice> endpoint;
            ComPtr<IAudioClient> client;
            ComPtr<IAudioRenderClient> render;
            HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(enumerator.GetAddressOf()));
            if (SUCCEEDED(result)) result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, endpoint.GetAddressOf());
            if (SUCCEEDED(result)) result = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(client.GetAddressOf()));
            WAVEFORMATEX format {};
            format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format.nChannels = 2;
            format.nSamplesPerSec = static_cast<DWORD>(juce::roundToInt(rate));
            format.wBitsPerSample = 32;
            format.nBlockAlign = 8;
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            // Windows converts this stream to its existing shared mix format.
            // No input, ASIO driver or DAW sample-rate configuration is opened.
            if (SUCCEEDED(result)) result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                1000000, 0, &format, nullptr);
            UINT32 capacity = 0;
            if (SUCCEEDED(result)) result = client->GetBufferSize(&capacity);
            if (SUCCEEDED(result)) result = client->GetService(IID_PPV_ARGS(render.GetAddressOf()));
            if (FAILED(result) || capacity == 0)
            { failure(FAILED(result) ? result : E_FAIL); return; }
            DWORD taskIndex = 0;
            struct AudioTask
            {
                HANDLE handle;
                ~AudioTask() { if (handle != nullptr) AvRevertMmThreadCharacteristics(handle); }
            } task { AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex) };
            const int length = audio.getNumSamples();
            int cursor = juce::jlimit(0, length, juce::roundToInt(seconds.load() * rate));
            unsigned appliedSeek = seekRevision.load();
            bool started = false, draining = false;
            while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT)
            {
                const auto revision = seekRevision.load();
                if (revision != appliedSeek)
                {
                    if (started) { result = client->Stop(); if (FAILED(result)) break; }
                    result = client->Reset(); if (FAILED(result)) break;
                    cursor = juce::jlimit(0, length, juce::roundToInt(requestedSeek.load() * rate));
                    appliedSeek = revision; draining = false; started = false;
                }
                UINT32 padding = 0;
                result = client->GetCurrentPadding(&padding);
                if (FAILED(result)) break;
                if (draining)
                {
                    if (padding == 0) { seconds.store(length / rate); break; }
                }
                else if (padding < capacity)
                {
                    const UINT32 count = capacity - padding;
                    BYTE* data = nullptr;
                    result = render->GetBuffer(count, &data);
                    if (FAILED(result)) break;
                    auto* output = reinterpret_cast<float*>(data);
                    float maximum = 0.0f;
                    for (UINT32 i = 0; i < count; ++i)
                    {
                        if (cursor >= length)
                        {
                            if (looping.load()) cursor = 0;
                            else draining = true;
                        }
                        for (int c = 0; c < 2; ++c)
                        {
                            const float value = draining ? 0.0f : audio.getSample(juce::jmin(c, audio.getNumChannels() - 1), cursor);
                            *output++ = value;
                            maximum = juce::jmax(maximum, std::abs(value));
                        }
                        if (!draining) ++cursor;
                    }
                    result = render->ReleaseBuffer(count, 0);
                    if (FAILED(result)) break;
                    level.store(maximum);
                    if (seekRevision.load() == revision) seconds.store(cursor / rate);
                    if (!started)
                    {
                        result = client->Start(); if (FAILED(result)) break;
                        started = true; playing.store(true); notify({});
                    }
                }
                if (WaitForSingleObject(stopEvent, 10) != WAIT_TIMEOUT) break;
            }
            if (started) client->Stop();
            if (!notified) failure(FAILED(result) ? result : E_ABORT);
        }
        catch (const std::exception& error)
        { notify(juce::String::fromUTF8(u8"Не удалось запустить плеер Windows: ") + juce::String::fromUTF8(error.what())); }
        catch (...) { notify(juce::String::fromUTF8(u8"Не удалось запустить плеер Windows")); }
        playing.store(false); level.store(0.0f);
    }
};

SystemPreview::SystemPreview() : impl(std::make_unique<Impl>()) {}
SystemPreview::~SystemPreview() { stop(); }
bool SystemPreview::start(const juce::MemoryBlock& wave, double startSeconds, bool loop, juce::String& error)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    stop();
    if (impl->stopEvent == nullptr)
    { error = juce::String::fromUTF8(u8"Windows не создала событие аудиоплеера"); return false; }
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(wave, false), true));
    if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > 23040000
        || reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate < 8000 || reader->sampleRate > 192000)
    { error = juce::String::fromUTF8(u8"Нет подходящего аудио для прослушивания"); return false; }
    impl->audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&impl->audio, 0, impl->audio.getNumSamples(), 0, true, true))
    { error = juce::String::fromUTF8(u8"Не удалось прочитать аудио для прослушивания"); return false; }
    impl->rate = reader->sampleRate;
    impl->seconds.store(juce::jlimit(0.0, impl->audio.getNumSamples() / impl->rate, startSeconds));
    impl->seekRevision.store(0); impl->looping.store(loop);
    ResetEvent(impl->stopEvent);
    std::promise<juce::String> ready;
    auto startup = ready.get_future();
    impl->worker = std::thread([state = impl.get(), ready = std::move(ready)]() mutable { state->run(std::move(ready)); });
    error = startup.get();
    if (error.isNotEmpty()) { stop(); return false; }
    return true;
}
void SystemPreview::stop()
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (impl->stopEvent != nullptr) SetEvent(impl->stopEvent);
    if (impl->worker.joinable()) impl->worker.join();
    impl->playing.store(false); impl->level.store(0.0f);
    impl->audio.setSize(0, 0);
}
bool SystemPreview::isPlaying() const { return impl->playing.load(); }
double SystemPreview::position() const { return impl->seconds.load(); }
void SystemPreview::seek(double positionSeconds)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    const double duration = impl->rate > 0 ? impl->audio.getNumSamples() / impl->rate : 0.0;
    const double target = juce::jlimit(0.0, duration, positionSeconds);
    impl->requestedSeek.store(target); impl->seekRevision.fetch_add(1); impl->seconds.store(target);
}
void SystemPreview::setLooping(bool loop) { impl->looping.store(loop); }
float SystemPreview::peak() const { return impl->playing.load() ? impl->level.load() : 0.0f; }
}
