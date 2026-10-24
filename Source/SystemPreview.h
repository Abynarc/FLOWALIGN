#pragma once
#include <JuceHeader.h>

namespace flowalign
{
// Output-only audition. No input device, device configuration or host callback.
// Called outside the real-time audio callback.
class SystemPreview final
{
public:
    SystemPreview();
    ~SystemPreview();
    bool start(const juce::MemoryBlock& wave, double position, bool loop, juce::String& error);
    void stop();
    bool isPlaying() const;
    double position() const;
    void seek(double);
    void setLooping(bool);
    float peak() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemPreview)
};
}
