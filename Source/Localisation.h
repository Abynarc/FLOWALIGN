#pragma once
#include <JuceHeader.h>
#include "LocalisationStrings.h"
#include <map>
#include "PlatformText.h"
namespace flowui
{
enum class Language { russian, english };
inline juce::String platform(juce::String value, bool english = false) { return flowalign::platformText(value, english); }
inline juce::String text(const juce::String& value, Language language)
{
    // Built once on the message thread; never called from the audio callback.
    static const auto lookup = []
    {
        std::map<juce::String, std::pair<juce::String, juce::String>> result;
        for (const auto& entry : translations)
        {
            const auto ru = juce::String::fromUTF8(entry.ru), en = juce::String::fromUTF8(entry.en);
            const auto pair = std::make_pair(platform(ru), platform(en, true));
            for (const auto& key : { ru, en, pair.first, pair.second }) result.emplace(key, pair);
        }
        return result;
    }();
    const auto found = lookup.find(value);
    if (found == lookup.end()) return value;
    return language == Language::english ? found->second.second : found->second.first;
}
inline juce::String message(const juce::String& value, Language language)
{
    const auto exact = text(value, language);
    if (exact != value) return exact;
    // Keep filenames opaque, even if a filename contains a translated word.
    for (const auto* key : { u8"Загрузка: ", u8"Регион загружен: ", u8"Файл загружен: " })
    {
        const auto prefix = platform(juce::String::fromUTF8(key));
        if (value.startsWith(prefix)) return text(prefix, language) + value.substring(prefix.length());
    }
    // Only known diagnostic fragments are composed. Never scan arbitrary user text.
    const char* prefixes[] = { u8"Не удалось обработать аудио: ", u8"Logic не передал регион: ",
        u8"Не удалось запустить плеер Windows: ", u8"Выбрана пара ", u8"Пара ",
        u8"Перетащите результат в Logic. Начало дубля: ", u8"длины регионов различаются: ",
        u8"Windows не открыла системный аудиовыход (WASAPI 0x", u8"Эталон", u8"Дубль", u8"Результат" };
    for (const auto* key : prefixes)
    {
        const auto prefix = platform(juce::String::fromUTF8(key));
        if (!value.startsWith(prefix) || value.length() <= prefix.length()) continue;
        auto rest = value.substring(prefix.length());
        for (const auto* suffixKey : { u8" · готово", u8" · записано", u8" · запись", u8" с", u8" · прослушивание через системный выход macOS",
              u8"). Проверьте выход звука и эксклюзивный режим ASIO", u8" с. Обрежьте их одинаково; для другой пары нажмите «+ Фраза»" })
        {
            const auto suffix = platform(juce::String::fromUTF8(suffixKey));
            if (rest.endsWith(suffix))
            {
                auto middle = rest.dropLastCharacters(suffix.length());
                const auto separator = juce::String::fromUTF8(u8" с и ");
                // The only embedded separator occurs between two durations.
                if (middle.containsOnly("0123456789., " + separator))
                    middle = middle.replace(separator, text(separator, language));
                return text(prefix, language) + middle + text(suffix, language);
            }
        }
        return text(prefix, language) + text(rest, language);
    }
    return exact;
}
// Refresh visible controls without notifications: translating a preset must not apply it.
inline void refresh(juce::Component& component, Language language)
{
    if (auto* button = dynamic_cast<juce::Button*>(&component))
        button->setButtonText(text(button->getButtonText(), language));
    if (auto* tooltip = dynamic_cast<juce::SettableTooltipClient*>(&component))
        tooltip->setTooltip(text(tooltip->getTooltip(), language));
    if (auto* box = dynamic_cast<juce::ComboBox*>(&component))
    {
        const int selected = box->getSelectedId();
        const auto display = box->getText();
        for (int i = 0; i < box->getNumItems(); ++i)
            box->changeItemText(box->getItemId(i), text(box->getItemText(i), language));
        if (selected != 0) box->setSelectedId(selected, juce::dontSendNotification);
        else box->setText(text(display, language), juce::dontSendNotification);
    }
    for (auto* child : component.getChildren()) refresh(*child, language);
}
}
