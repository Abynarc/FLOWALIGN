#include "AlignmentEngine.h"
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace flowalign
{
namespace
{
void checkCancel(const std::atomic<bool>& cancelled)
{
    if (cancelled.load()) throw std::runtime_error("cancelled");
}

std::vector<float> mono(const juce::AudioBuffer<float>& audio)
{
    std::vector<float> result(static_cast<size_t>(audio.getNumSamples()), 0.0f);
    int strongest = 0;
    float maximum = -1.0f;
    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        const float rms = audio.getRMSLevel(c, 0, audio.getNumSamples());
        if (rms > maximum) { strongest = c; maximum = rms; }
    }
    // A stereo pair may be out of phase. Summing it could erase the voice.
    std::copy_n(audio.getReadPointer(strongest), audio.getNumSamples(), result.begin());
    return result;
}

struct Features { std::vector<float> energy, onset; };
Features features(const std::vector<float>& samples, int frames, int hop)
{
    Features f;
    f.energy.resize(static_cast<size_t>(frames));
    f.onset.resize(static_cast<size_t>(frames));
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        double sum = 0.0;
        const int first = juce::jmax(0, i * hop - hop / 2);
        const int last = juce::jmin(static_cast<int>(samples.size()), i * hop + 3 * hop / 2);
        for (int s = first; s < last; ++s) sum += static_cast<double>(samples[static_cast<size_t>(s)]) * samples[static_cast<size_t>(s)];
        f.energy[static_cast<size_t>(i)] = static_cast<float>(std::sqrt(sum / juce::jmax(1, last - first)));
        peak = juce::jmax(peak, f.energy[static_cast<size_t>(i)]);
    }
    if (peak < 0.00001f) throw std::runtime_error("silent");
    for (auto& v : f.energy) v = std::log1p(30.0f * v / peak) / std::log(31.0f);
    for (int i = 1; i < frames; ++i)
        f.onset[static_cast<size_t>(i)] = juce::jmax(0.0f, f.energy[static_cast<size_t>(i)] - f.energy[static_cast<size_t>(i - 1)]);
    return f;
}

float sample(const std::vector<float>& v, int i)
{
    return juce::isPositiveAndBelow(i, static_cast<int>(v.size())) ? v[static_cast<size_t>(i)] : 0.0f;
}

double mappedSample(const std::vector<double>& map, double output, int hop)
{
    const double frame = juce::jlimit(0.0, static_cast<double>(map.size() - 1), output / hop);
    const auto index = static_cast<size_t>(frame);
    const auto next = juce::jmin(index + 1, map.size() - 1);
    return (map[index] + (frame - static_cast<double>(index)) * (map[next] - map[index])) * hop;
}
}

juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& source, double fromRate, double toRate)
{
    if (std::abs(fromRate - toRate) < 0.01) return juce::AudioBuffer<float>(source);
    const int length = static_cast<int>(std::ceil(source.getNumSamples() * toRate / fromRate));
    juce::AudioBuffer<float> result(source.getNumChannels(), length);
    const double ratio = fromRate / toRate;
    const double latency = juce::WindowedSincInterpolator::getBaseLatency() / ratio;
    const int extra = static_cast<int>(std::ceil(latency)) + 2;
    std::vector<float> temporary(static_cast<size_t>(length + extra));
    for (int c = 0; c < source.getNumChannels(); ++c)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process(ratio, source.getReadPointer(c), temporary.data(),
                             length + extra, source.getNumSamples(), 0);
        for (int i = 0; i < length; ++i)
        {
            const double position = i + latency;
            const int first = static_cast<int>(position);
            const float alpha = static_cast<float>(position - first);
            result.setSample(c, i, temporary[static_cast<size_t>(first)] * (1.0f - alpha)
                + temporary[static_cast<size_t>(first + 1)] * alpha);
        }
    }
    return result;
}

Alignment align(const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& dub,
                double rate, const Settings& settings, std::atomic<float>& progress,
                const std::atomic<bool>& cancelled)
{
    if (rate < 8000.0 || dub.getNumChannels() == 0 || reference.getNumChannels() == 0)
        throw std::runtime_error("invalid");
    const int length = juce::jmax(reference.getNumSamples(), dub.getNumSamples());
    if (length < static_cast<int>(rate * 0.1) || length > static_cast<int>(rate * 120.0))
        throw std::runtime_error("length");
    const int hop = juce::jmax(1, static_cast<int>(rate * 0.01));
    const int n = (length + hop - 1) / hop;
    const auto refMono = mono(reference), dubMono = mono(dub);
    const auto ref = features(refMono, n, hop), target = features(dubMono, n, hop);
    progress.store(0.12f);
    // Limit displacement to three seconds; banded storage keeps memory bounded.
    const int band = juce::jmin(n, 300), width = 2 * band + 1;
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<float> previous(static_cast<size_t>(n + 1), infinity), current(previous);
    std::vector<uint8_t> directions(static_cast<size_t>(n) * static_cast<size_t>(width), 0);
    previous[0] = 0.0f;
    for (int i = 1; i <= n; ++i)
    {
        checkCancel(cancelled);
        std::fill(current.begin(), current.end(), infinity);
        for (int j = juce::jmax(1, i - band); j <= juce::jmin(n, i + band); ++j)
        {
            const auto a = static_cast<size_t>(i - 1), b = static_cast<size_t>(j - 1);
            const float cost = std::abs(ref.energy[a] - target.energy[b])
                             + 1.8f * std::abs(ref.onset[a] - target.onset[b]);
            float best = previous[static_cast<size_t>(j - 1)];
            uint8_t direction = 1;
            if (previous[static_cast<size_t>(j)] + 0.045f < best)
            { best = previous[static_cast<size_t>(j)] + 0.045f; direction = 2; }
            if (current[static_cast<size_t>(j - 1)] + 0.045f < best)
            { best = current[static_cast<size_t>(j - 1)] + 0.045f; direction = 3; }
            current[static_cast<size_t>(j)] = cost + best;
            directions[static_cast<size_t>(i - 1) * static_cast<size_t>(width) + static_cast<size_t>(j - i + band)] = direction;
        }
        previous.swap(current);
        progress.store(0.12f + 0.36f * static_cast<float>(i) / static_cast<float>(n));
    }
    std::vector<double> map(static_cast<size_t>(n + 1), 0.0);
    std::vector<int> counts(static_cast<size_t>(n + 1), 0);
    for (int i = n, j = n; i > 0 && j > 0;)
    {
        map[static_cast<size_t>(i)] += j;
        ++counts[static_cast<size_t>(i)];
        const auto direction = directions[static_cast<size_t>(i - 1) * static_cast<size_t>(width) + static_cast<size_t>(j - i + band)];
        if (direction == 1) { --i; --j; }
        else if (direction == 2) --i;
        else if (direction == 3) --j;
        else throw std::runtime_error("path");
    }
    for (int i = 1; i <= n; ++i)
        map[static_cast<size_t>(i)] = counts[static_cast<size_t>(i)] > 0
            ? map[static_cast<size_t>(i)] / counts[static_cast<size_t>(i)] : map[static_cast<size_t>(i - 1)] + 1.0;
    const auto raw = map;
    const int radius = 1 + static_cast<int>(settings.naturalness * 8.0f);
    for (int i = 1; i < n; ++i)
    {
        double offset = 0.0;
        int count = 0;
        for (int k = juce::jmax(0, i - radius); k <= juce::jmin(n, i + radius); ++k)
        { offset += raw[static_cast<size_t>(k)] - k; ++count; }
        map[static_cast<size_t>(i)] = i + settings.tightness * offset / count;
    }
    // Both endpoints remain fixed. Bound the derivative of the time map.
    map.front() = 0.0; map.back() = n;
    const double minimum = 1.0 / (1.0 + settings.maxStretch), maximum = 1.0 + settings.maxStretch;
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int i = 1; i < n; ++i)
            map[static_cast<size_t>(i)] = juce::jlimit(map[static_cast<size_t>(i - 1)] + minimum,
                                                     map[static_cast<size_t>(i - 1)] + maximum, map[static_cast<size_t>(i)]);
        for (int i = n - 1; i > 0; --i)
            map[static_cast<size_t>(i)] = juce::jlimit(map[static_cast<size_t>(i + 1)] - maximum,
                                                     map[static_cast<size_t>(i + 1)] - minimum, map[static_cast<size_t>(i)]);
    }
    Alignment result;
    result.audio.setSize(dub.getNumChannels(), length);
    result.audio.clear();
    result.sourceSeconds.reserve(map.size());
    for (int i = 0; i <= n; ++i)
    {
        result.sourceSeconds.push_back(map[static_cast<size_t>(i)] * hop / rate);
        result.meanCorrectionMs += std::abs(map[static_cast<size_t>(i)] - i) * hop / rate * 1000.0 / (n + 1);
    }
    progress.store(0.5f);
    if (settings.tightness <= 0.0001f)
    {
        for (int c = 0; c < dub.getNumChannels(); ++c)
            result.audio.copyFrom(c, 0, dub, c, 0, dub.getNumSamples());
    }
    else if (!settings.preservePitch)
    {
        for (int i = 0; i < length; ++i)
        {
            if (i % 4096 == 0) checkCancel(cancelled);
            const double position = mappedSample(map, i, hop);
            const int first = static_cast<int>(position);
            const float alpha = static_cast<float>(position - first);
            for (int c = 0; c < dub.getNumChannels(); ++c)
                if (first + 1 < dub.getNumSamples())
                    result.audio.setSample(c, i, dub.getSample(c, first) * (1.0f - alpha) + dub.getSample(c, first + 1) * alpha);
        }
    }
    else
    {
        // One source offset is shared by every channel to preserve stereo phase.
        const int window = juce::jmax(64, static_cast<int>(rate * (settings.protectTransients ? 0.03 : 0.05)));
        const int synthesisHop = window / 4;
        const int search = static_cast<int>(rate * 0.008);
        const int stride = juce::jmax(1, static_cast<int>(rate / 12000.0));
        std::vector<float> weights(static_cast<size_t>(length), 0.0f), hann(static_cast<size_t>(window));
        for (int k = 0; k < window; ++k)
            hann[static_cast<size_t>(k)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(k) / static_cast<float>(window));
        int lastSource = -window / 2;
        for (int out = -window / 2; out < length; out += synthesisHop)
        {
            checkCancel(cancelled);
            const int expected = static_cast<int>(std::llround(mappedSample(map, out + window / 2, hop))) - window / 2;
            int chosen = expected;
            if (out > -window / 2)
            {
                const int natural = lastSource + synthesisHop;
                double best = -2.0;
                auto score = [&](int candidate)
                {
                    double dot = 0.0, aa = 0.0, bb = 0.0;
                    for (int k = 0; k < window - synthesisHop; k += stride)
                    {
                        const double a = sample(dubMono, natural + k), b = sample(dubMono, candidate + k);
                        dot += a * b; aa += a * a; bb += b * b;
                    }
                    const double similarity = dot / std::sqrt(aa * bb + 1.0e-20)
                        - 0.025 * std::abs(candidate - expected) / juce::jmax(1, search);
                    if (similarity > best) { best = similarity; chosen = candidate; }
                };
                score(expected);
                for (int candidate = expected - search; candidate <= expected + search; candidate += stride * 2) score(candidate);
                const int coarse = chosen;
                for (int candidate = coarse - stride * 2; candidate <= coarse + stride * 2; ++candidate) score(candidate);
            }
            lastSource = chosen;
            for (int k = 0; k < window; ++k)
            {
                const int output = out + k, input = chosen + k;
                if (!juce::isPositiveAndBelow(output, length)) continue;
                const float weight = hann[static_cast<size_t>(k)];
                weights[static_cast<size_t>(output)] += weight;
                if (!juce::isPositiveAndBelow(input, dub.getNumSamples())) continue;
                for (int c = 0; c < dub.getNumChannels(); ++c)
                    result.audio.addSample(c, output, weight * dub.getSample(c, input));
            }
            progress.store(0.5f + 0.47f * static_cast<float>(juce::jmax(0, out)) / static_cast<float>(length));
        }
        for (int i = 0; i < length; ++i)
            if (weights[static_cast<size_t>(i)] > 0.000001f)
                for (int c = 0; c < result.audio.getNumChannels(); ++c)
                    result.audio.setSample(c, i, result.audio.getSample(c, i) / weights[static_cast<size_t>(i)]);
    }
    checkCancel(cancelled);
    if (settings.autoGain)
    {
        double before = 0.0, after = 0.0;
        float peak = 0.0f;
        for (int c = 0; c < dub.getNumChannels(); ++c)
        {
            before += std::pow(dub.getRMSLevel(c, 0, dub.getNumSamples()), 2.0);
            after += std::pow(result.audio.getRMSLevel(c, 0, length), 2.0);
            peak = juce::jmax(peak, result.audio.getMagnitude(c, 0, length));
        }
        const float gain = juce::jlimit(0.5f, 2.0f, static_cast<float>(std::sqrt(before / (after + 1.0e-20))));
        result.audio.applyGain(juce::jmin(gain, peak > 0.0f ? 0.999f / peak : 1.0f));
    }
    progress.store(1.0f);
    return result;
}
}
