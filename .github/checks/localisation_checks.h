#pragma once
#include <cstdlib>
#include <functional>

static flowui::Language testLanguage()
{
    const auto* value = std::getenv("FLOW_UI_TEST_LANGUAGE");
    return value && juce::String(value) == "en" ? flowui::Language::english : flowui::Language::russian;
}
static bool hasCyrillic(const juce::String& text)
{
    for (auto c : text) if (c >= 0x400 && c <= 0x4ff) return true;
    return false;
}
static void visitUI(juce::Component& component, const std::function<void(juce::Component&)>& visitor)
{
    visitor(component);
    for (auto* child : component.getChildren()) visitUI(*child, visitor);
}
template <typename Processor>
static void localisationChecks(const juce::File& folder)
{
    using Language = flowui::Language;
    Processor p, other;
    require(p.uiLanguage() == Language::russian, "New instance language is not Russian");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    juce::ComboBox* picker = nullptr;
    visitUI(*editor, [&](juce::Component& c) { if (c.getComponentID() == "uiLanguage") picker = dynamic_cast<juce::ComboBox*>(&c); });
    require(picker && picker->getNumItems() == 2, "Language picker missing");
    std::vector<float> parameterValues;
    for (auto* parameter : p.getParameters()) parameterValues.push_back(parameter->getValue());
    for (int selected : { 2, 1, 2, 1, 2 })
    {
        picker->setSelectedId(selected, juce::sendNotificationSync);
        juce::Timer::callPendingTimersSynchronously();
        require(p.uiLanguage() == (selected == 2 ? Language::english : Language::russian), "Picker did not change language");
        require(other.uiLanguage() == Language::russian, "Language leaked to another instance");
        for (int i = 0; i < p.getParameters().size(); ++i)
            require(p.getParameters()[i]->getValue() == parameterValues[static_cast<size_t>(i)], "Language switch changed a DSP parameter or applied a preset");
        visitUI(*editor, [&](juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::Button*>(&c))
                require(b->getButtonText() == flowui::text(b->getButtonText(), p.uiLanguage()), "Button language did not follow picker");
        });
        if (selected == 2)
            visitUI(*editor, [&](juce::Component& c)
            {
                if (c.getComponentID() == "uiLanguage") return;
                if (auto* button = dynamic_cast<juce::Button*>(&c)) require(!hasCyrillic(button->getButtonText()), "Untranslated button in English UI");
                if (auto* tooltip = dynamic_cast<juce::SettableTooltipClient*>(&c))
                    if (tooltip->getTooltip() != picker->getTooltip()) require(!hasCyrillic(tooltip->getTooltip()), "Untranslated tooltip in English UI");
                if (auto* box = dynamic_cast<juce::ComboBox*>(&c))
                {
                    require(!hasCyrillic(box->getText()), "Stale selected combo text in English UI");
                    for (int i = 0; i < box->getNumItems(); ++i) require(!hasCyrillic(box->getItemText(i)), "Untranslated menu item");
                }
            });
    }
    for (auto language : { Language::russian, Language::english })
    {
        picker->setSelectedId(language == Language::english ? 2 : 1, juce::sendNotificationSync);juce::Timer::callPendingTimersSynchronously();
        for (int width : { 1000, 1280, 1600 })
        {
            editor->setSize(width, juce::roundToInt(width / editor->getConstrainer()->getFixedAspectRatio()));
            require(editor->getLocalBounds().contains(picker->getBounds()), "Language picker is outside the editor");
            for (auto* sibling : editor->getChildren()) if (sibling != picker && sibling->isVisible())
                require(!sibling->getBounds().intersects(picker->getBounds()), "Language picker overlaps another control");
            auto picture = editor->createComponentSnapshot(editor->getLocalBounds());
            const auto file = folder.getChildFile(juce::String(language == Language::english ? "en-" : "ru-") + juce::String(width) + ".png");
            file.deleteFile();auto output = file.createOutputStream();require(output && juce::PNGImageFormat().writeImageToStream(picture, *output), "Could not save localized editor screenshot");
        }
        juce::MemoryBlock state;p.getStateInformation(state);
        Processor restored;restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(restored.uiLanguage() == language, "Language was not saved with the project");
        auto reopened = std::unique_ptr<juce::AudioProcessorEditor>(restored.createEditor());
        juce::ComboBox* reopenedPicker = nullptr;
        visitUI(*reopened, [&](juce::Component& c) { if (c.getComponentID() == "uiLanguage") reopenedPicker = dynamic_cast<juce::ComboBox*>(&c); });
        require(reopenedPicker && reopenedPicker->getSelectedId() == (language == Language::english ? 2 : 1), "Editor recreation lost language");
        // Restore state while an editor is already open.
        other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(other.uiLanguage() == language, "State load did not restore UI language");
    }
    const auto filename = juce::String::fromUTF8(u8"Пара Эталон Дубль.wav");
    const auto loading = juce::String::fromUTF8(u8"Загрузка: ") + filename;
    require(flowui::message(loading, Language::english) == "Loading: " + filename || !p.getName().contains("ALIGN"), "Localisation changed a filename");
    if (p.getName().contains("ALIGN"))
    {
        const auto diagnostic = juce::String::fromUTF8(u8"Не удалось обработать аудио: длины регионов различаются: 3.500000 с и 4.000000 с. Обрежьте их одинаково; для другой пары нажмите «+ Фраза»");
        const auto translated = p.uiMessage(diagnostic);
        require(!hasCyrillic(translated) && translated.contains("3.500000 s and 4.000000 s"), "Nested import diagnostic was not fully translated");
        require(p.uiMessage(juce::String::fromUTF8(u8"Пара 8 · готово")) == "Pair 8 · ready", "Pair status was not translated");
    }
    for (const auto& entry : flowui::translations)
    {
        const auto english = flowui::text(juce::String::fromUTF8(entry.ru), Language::english);
        require(!hasCyrillic(english), "Catalog entry is not fully English");
    }
    // Old state without a language property must select Russian, even when loading over EN.
    juce::MemoryBlock oldState;
    if (p.getName().contains("TONE"))
    {
        auto tree = p.parameters.copyState();tree.removeProperty("uiLanguage", nullptr);
        if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, oldState);
    }
    else
    {
        juce::MemoryOutputStream destination(oldState, false);destination.writeInt(0x46414c33);
        juce::GZIPCompressorOutputStream compressed(&destination, 1, false);
        auto tree = p.parameters.copyState();tree.removeProperty("uiLanguage", nullptr);tree.writeToStream(compressed);
        compressed.writeInt(1);compressed.writeInt(0);compressed.writeInt(0);compressed.writeString("");
        compressed.writeDouble(-1);compressed.writeDouble(-1);compressed.writeDouble(0);compressed.writeBool(false);
        compressed.writeFloat(1);compressed.writeFloat(1);compressed.writeFloat(.3f);
        compressed.writeBool(true);compressed.writeBool(true);compressed.writeBool(false);
        for (int i = 0; i < 3; ++i) compressed.writeBool(false);
        compressed.writeInt(0);compressed.writeInt(0x454e4433);
    }
    p.setUiLanguage(Language::english);p.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    require(p.uiLanguage() == Language::russian, "Old project did not default to Russian");
    juce::Thread::sleep(60);juce::Timer::callPendingTimersSynchronously();require(picker->getSelectedId() == 1, "Open editor did not follow restored language");
    std::cout << "PASS: RU/EN picker, independent instances, unchanged DSP parameters, translated controls/menus/tooltips, 1000/1280/1600 layouts, project state, editor recreation, legacy state and catalog\n";
}

template <typename Processor>
static void toggleLanguageRoundTrip(juce::AudioProcessorEditor& editor, Processor& processor)
{
    juce::ComboBox* picker = nullptr;
    visitUI(editor, [&](juce::Component& c) { if (c.getComponentID() == "uiLanguage") picker = dynamic_cast<juce::ComboBox*>(&c); });
    require(picker != nullptr, "Missing language picker during workflow");
    const int selected = processor.uiLanguage() == flowui::Language::english ? 2 : 1;
    picker->setSelectedId(selected == 2 ? 1 : 2, juce::sendNotificationSync);
    picker->setSelectedId(selected, juce::sendNotificationSync);
}
