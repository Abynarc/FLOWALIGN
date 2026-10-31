#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace flowalign
{
struct Settings
{
    float tightness = 1.0f, naturalness = 1.0f, maxStretch = 0.3f;
    bool preservePitch = true, protectTransients = true, autoGain = false;
};

struct Alignment
{
    juce::AudioBuffer<float> audio;
    std::vector<double> sourceSeconds;
    double meanCorrectionMs = 0.0;
};

// Offline DTW on energy/onset envelopes, followed by stereo-linked WSOLA.
// No pitch resampling is used when preservePitch is enabled.
Alignment align(const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& dub,
                double sampleRate, const Settings&, std::atomic<float>& progress,
                const std::atomic<bool>& cancelled);
juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>&, double fromRate, double toRate);
}
