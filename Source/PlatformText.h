#pragma once
#include <JuceHeader.h>

namespace flowalign
{
// Keep the existing Logic UI on macOS; Windows labels apply to any VST3 host.
inline juce::String platformText(const char* text)
{
    auto result = juce::String::fromUTF8(text);
#if JUCE_WINDOWS
    result = result.replace("Logic Pro", "DAW").replace("Logic", "DAW").replace("LOGIC", "DAW")
                   .replace("macOS", "Windows").replace("Finder", juce::String::fromUTF8(u8"Проводник"))
                   .replace("FINDER", juce::String::fromUTF8(u8"ПРОВОДНИК"));
#endif
    return result;
}
}
