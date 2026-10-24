#import <AVFoundation/AVFoundation.h>
#include "SystemPreview.h"
#include <mutex>

namespace flowalign
{
struct SystemPreview::Impl
{
    AVAudioPlayer* player = nil;
    mutable std::recursive_mutex mutex;
};

SystemPreview::SystemPreview() : impl(std::make_unique<Impl>()) {}
SystemPreview::~SystemPreview() { stop(); }

bool SystemPreview::start(const juce::MemoryBlock& wave, double positionSeconds, bool looping, juce::String& error)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    stop();
    @autoreleasepool
    {
        if (wave.getSize() < 44)
        { error = juce::String::fromUTF8(u8"Нет аудио для прослушивания"); return false; }
        NSData* data = [[NSData alloc] initWithBytes:wave.getData() length:wave.getSize()];
        NSError* reason = nil;
        impl->player = [[AVAudioPlayer alloc] initWithData:data error:&reason];
        [data release];
        if (impl->player == nil)
        {
            error = juce::String::fromUTF8(u8"Не удалось открыть аудио для прослушивания");
            if (reason != nil) error += ": " + juce::String::fromUTF8([[reason localizedDescription] UTF8String]);
            return false;
        }
        impl->player.volume = 1.0f;
        impl->player.meteringEnabled = YES;
        impl->player.numberOfLoops = looping ? -1 : 0;
        impl->player.currentTime = juce::jlimit(0.0, impl->player.duration, positionSeconds);
        if (![impl->player prepareToPlay] || ![impl->player play])
        {
            error = juce::String::fromUTF8(u8"macOS не запустила аудиовыход. Проверьте системный выход звука");
            stop(); return false;
        }
        return true;
    }
}

void SystemPreview::stop()
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    @autoreleasepool
    {
        [impl->player stop];
        [impl->player release];
        impl->player = nil;
    }
}

bool SystemPreview::isPlaying() const
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return impl->player != nil && impl->player.isPlaying;
}
double SystemPreview::position() const
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return impl->player != nil ? impl->player.currentTime : 0.0;
}
void SystemPreview::seek(double seconds)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (impl->player != nil) impl->player.currentTime = juce::jlimit(0.0, impl->player.duration, seconds);
}
void SystemPreview::setLooping(bool looping)
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (impl->player != nil) impl->player.numberOfLoops = looping ? -1 : 0;
}
float SystemPreview::peak() const
{
    const std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (!isPlaying()) return 0.0f;
    [impl->player updateMeters];
    float maximum = -160.0f;
    for (NSUInteger c = 0; c < impl->player.numberOfChannels; ++c)
        maximum = juce::jmax(maximum, [impl->player peakPowerForChannel:c]);
    return juce::Decibels::decibelsToGain(maximum);
}
}
