#pragma once
#include <JuceHeader.h>

namespace flowalign
{
// Keep the existing Logic UI on macOS; Windows labels apply to any VST3 host.
inline juce::String platformText(juce::String result, bool english = false)
{
#if JUCE_WINDOWS
    if (english)
        return result.replace("Logic Pro", "DAW").replace("Logic", "DAW").replace("LOGIC", "DAW")
                     .replace("macOS", "Windows").replace("WAV IN FINDER", "OPEN WAV")
                     .replace("WAV in Finder", "Open WAV").replace("Finder", "File Explorer").replace("FINDER", "FILE EXPLORER");
    result = result.replace("Logic Pro", "DAW").replace("Logic", "DAW").replace("LOGIC", "DAW")
                   .replace("macOS", "Windows")
                   .replace("WAV В FINDER", juce::String::fromUTF8(u8"ОТКРЫТЬ WAV"))
                   .replace(juce::String::fromUTF8(u8"«WAV в Finder»"), juce::String::fromUTF8(u8"«Открыть WAV»"))
                   .replace("в Finder", juce::String::fromUTF8(u8"в Проводнике"))
                   .replace("из Finder", juce::String::fromUTF8(u8"из Проводника"))
                   .replace("через Finder", juce::String::fromUTF8(u8"через Проводник"))
                   .replace("Finder", juce::String::fromUTF8(u8"Проводник"))
                   .replace("FINDER", juce::String::fromUTF8(u8"ПРОВОДНИК"));
#else
    juce::ignoreUnused(english);
#endif
    return result;
}
inline juce::String platformText(const char* text) { return platformText(juce::String::fromUTF8(text)); }
}
