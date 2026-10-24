#include "PluginEditor.h"
#include <cmath>
#include "PlatformText.h"

namespace
{
using Track = FLOWALIGNAudioProcessor::Track;
const juce::Colour green(0xff5aff91), muted(0xff8faaa4), border(0xff2a3e3e), white(0xffeef5f4);
juce::String ru(const char* text) { return flowalign::platformText(text); }
void font(juce::Graphics& g, float size, bool bold = false, float spacing = 0.0f)
{
    juce::Font f(juce::FontOptions("Arial", size, bold ? juce::Font::bold : juce::Font::plain));
    f.setExtraKerningFactor(spacing); g.setFont(f);
}
void flowText(juce::Graphics& g, const juce::String& s, float size, juce::Rectangle<int> bounds,
          juce::Colour colour = white, bool bold = false, juce::Justification justification = juce::Justification::centredLeft)
{
    font(g, size, bold); g.setColour(colour); g.drawFittedText(s, bounds, justification, 1);
}
void panel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 15.0f)
{
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0b1516), r.getX(), r.getY(), juce::Colour(0xff060c0d), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle(r, radius); g.setColour(border); g.drawRoundedRectangle(r, radius, 1.0f);
    g.setColour(green.withAlpha(0.03f)); g.drawRoundedRectangle(r.reduced(1.0f), radius, 1.0f);
}
juce::String timeText(double seconds)
{
    const int total = static_cast<int>(juce::jmax(0.0, seconds));
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}
void microphone(juce::Graphics& g, float x, float y, juce::Colour colour)
{
    g.setColour(colour); g.drawRoundedRectangle(x + 9, y, 10, 22, 5, 1.8f);
    juce::Path p; p.startNewSubPath(x + 4, y + 13); p.lineTo(x + 4, y + 20);
    p.quadraticTo(x + 4, y + 29, x + 14, y + 29); p.quadraticTo(x + 24, y + 29, x + 24, y + 20); p.lineTo(x + 24, y + 13);
    g.strokePath(p, juce::PathStrokeType(1.8f)); g.drawLine(x + 14, y + 29, x + 14, y + 36, 1.8f); g.drawLine(x + 8, y + 36, x + 20, y + 36, 1.8f);
}
}

void FlowLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool active = button.getToggleState();
    const float alpha = button.isEnabled() ? 1.0f : 0.4f;
    g.setGradientFill(juce::ColourGradient((active ? green.brighter(0.1f) : juce::Colour(0xff152022)).withAlpha(alpha), 0, 0,
                                          (active ? juce::Colour(0xff3cdd79) : juce::Colour(0xff080d10)).withAlpha(alpha), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour((active || over ? green : border.brighter(0.15f)).withAlpha(alpha));
    g.drawRoundedRectangle(r, 9.0f, down || active ? 1.5f : 1.0f);
}

void FlowLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto icon = button.getComponentID();
    const bool previewIcon = icon.startsWith("preview");
    if (icon.startsWith("icon_") || previewIcon)
    {
        const float size = juce::jmin(button.getWidth(), button.getHeight()) * 0.36f;
        const juce::Point<float> centre(button.getWidth() * 0.5f, button.getHeight() * 0.5f);
        g.setColour((button.getToggleState() ? juce::Colour(0xff04100a) : white).withAlpha(button.isEnabled() ? 1.0f : 0.35f));
        juce::Path path;
        if (icon == "icon_play" || (previewIcon && !button.getToggleState()))
        { path.addTriangle(centre.x - size * 0.35f, centre.y - size * 0.5f, centre.x - size * 0.35f, centre.y + size * 0.5f, centre.x + size * 0.5f, centre.y); g.fillPath(path); }
        else if (icon == "icon_stop" || previewIcon) g.fillRect(juce::Rectangle<float>(size * 0.85f, size * 0.85f).withCentre(centre));
        else if (icon == "icon_loop")
        {
            path.addCentredArc(centre.x, centre.y, size * 0.52f, size * 0.52f, 0, -0.7f, 4.7f, true);
            g.strokePath(path, juce::PathStrokeType(1.5f));
            path.clear(); path.addTriangle(centre.x - size * 0.7f, centre.y - size * 0.15f, centre.x - size * 0.23f, centre.y - size * 0.2f, centre.x - size * 0.48f, centre.y + size * 0.2f); g.fillPath(path);
        }
        else if (icon == "icon_link")
        {
            g.drawRoundedRectangle(centre.x - size * 0.7f, centre.y - size * 0.13f, size * 0.8f, size * 0.48f, size * 0.2f, 1.5f);
            g.drawRoundedRectangle(centre.x - size * 0.1f, centre.y - size * 0.38f, size * 0.8f, size * 0.48f, size * 0.2f, 1.5f);
        }
        else if (icon == "icon_settings")
        {
            g.drawEllipse(juce::Rectangle<float>(size * 0.8f, size * 0.8f).withCentre(centre), 2.0f);
            g.drawEllipse(juce::Rectangle<float>(size * 0.25f, size * 0.25f).withCentre(centre), 1.5f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 8.0f;
                g.drawLine(centre.x + std::cos(a) * size * 0.37f, centre.y + std::sin(a) * size * 0.37f,
                           centre.x + std::cos(a) * size * 0.55f, centre.y + std::sin(a) * size * 0.55f, 2.0f);
            }
        }
        return;
    }
    const float size = juce::jlimit(10.0f, 16.0f, button.getHeight() * 0.28f);
    flowText(g, button.getButtonText(), size, button.getLocalBounds().reduced(8, 2),
         (button.getToggleState() ? juce::Colour(0xff04100a) : white).withAlpha(button.isEnabled() ? 1.0f : 0.35f),
         true, juce::Justification::centred);
}

void FlowLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over, bool)
{
    const float h = static_cast<float>(button.getHeight());
    const bool active = button.getToggleState();
    const auto bounds = button.getLocalBounds();
    flowText(g, button.getButtonText(), h * 0.48f, bounds.withTrimmedRight(static_cast<int>(h * 2.1f)), white);
    const juce::Rectangle<float> pill(button.getWidth() - h * 1.7f, h * 0.12f, h * 1.65f, h * 0.76f);
    g.setColour(active ? green.withAlpha(0.12f) : juce::Colour(0xff081010)); g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    g.setColour(active || over ? green : border); g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.1f);
    const float diameter = pill.getHeight() - 5.0f;
    g.setColour(active ? green : muted);
    g.fillEllipse(active ? pill.getRight() - diameter - 2.5f : pill.getX() + 2.5f, pill.getY() + 2.5f, diameter, diameter);
}

void FlowLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider&)
{
    const float radius = juce::jmin(static_cast<float>(width), static_cast<float>(height)) * 0.45f;
    const juce::Point<float> centre(x + width * 0.5f, y + height * 0.5f);
    const float angle = start + position * (end - start);
    juce::Path background, foreground;
    background.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
    foreground.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour(juce::Colour(0xff233133)); g.strokePath(background, juce::PathStrokeType(7.0f));
    g.setColour(green.withAlpha(0.08f)); g.strokePath(foreground, juce::PathStrokeType(17.0f));
    g.setColour(green); g.strokePath(foreground, juce::PathStrokeType(6.0f));
    auto knob = juce::Rectangle<float>(radius * 1.6f, radius * 1.6f).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff53615f), knob.getX(), knob.getY(), juce::Colour(0xff05090b), knob.getRight(), knob.getBottom(), false));
    g.fillEllipse(knob); g.setColour(juce::Colour(0xff41504f)); g.drawEllipse(knob, 1.0f);
    for (int i = 0; i < 96; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 96.0f;
        const float b = a + juce::MathConstants<float>::twoPi / 96.0f + 0.006f;
        const float shine = 0.08f + 0.55f * std::pow(std::abs(std::cos(a - 0.5f)), 12.0f);
        g.setColour(juce::Colour(0xff080e10).interpolatedWith(juce::Colour(0xff8c9998), shine));
        juce::Path sector; sector.startNewSubPath(centre); sector.lineTo(centre.x + std::cos(a) * radius * 0.75f, centre.y + std::sin(a) * radius * 0.75f);
        sector.lineTo(centre.x + std::cos(b) * radius * 0.75f, centre.y + std::sin(b) * radius * 0.75f); sector.closeSubPath(); g.fillPath(sector);
    }
    g.setColour(juce::Colour(0xff090f10)); g.drawEllipse(knob.reduced(4), 2.0f);
    const float px = centre.x + std::sin(angle) * radius * 0.64f, py = centre.y - std::cos(angle) * radius * 0.64f;
    g.setColour(green.withAlpha(0.15f)); g.fillEllipse(px - 8, py - 8, 16, 16);
    g.setColour(green); g.fillEllipse(px - 4, py - 4, 8, 8);
}

class FLOWALIGNAudioProcessorEditor::WavePanel final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    WavePanel(FLOWALIGNAudioProcessorEditor& e, Track t) : owner(e), track(t)
    {
        addAndMakeVisible(source); addAndMakeVisible(audition);
        audition.setComponentID(track == Track::reference ? "previewReference" : track == Track::dub ? "previewDub" : "previewResult");
        audition.onClick = [this] { owner.togglePreview(track); };
        audition.setTooltip(ru(u8"Прослушать только этот сигнал через системный выход macOS. Logic можно остановить; Mute дорожки не влияет на этот плеер."));
        source.onClick = [this]
        {
            if (track == Track::result) owner.revealResult();
            else
            {
                owner.captureTarget.setSelectedId(track == Track::reference ? 2 : 3, juce::dontSendNotification);
                owner.engine.startCapture(track == Track::reference ? FLOWALIGNAudioProcessor::CaptureMode::reference : FLOWALIGNAudioProcessor::CaptureMode::dub);
                owner.updateControls();
            }
        };
        if (track != Track::result)
        {
            nativeDrop = std::make_unique<flowalign::NativeRegionDrop>();
            nativeDrop->setComponentID(track == Track::reference ? "dropReference" : "dropDub");
            addAndMakeVisible(*nativeDrop);
            nativeDrop->canDrop = [this]
            {
                return !owner.engine.isCapturing() && !owner.engine.isCapturePending() && owner.pendingDrops.size() < 8;
            };
            nativeDrop->hover = [this](bool entered) { hovering = entered; repaint(); };
            nativeDrop->seekAt = [this](float x) { seekAt(x + static_cast<float>(nativeDrop->getX())); };
            nativeDrop->beginDrop = [this]() -> flowalign::NativeRegionDrop::Completion
            {
                const auto context = owner.engine.dropContext(track);
                const auto request = ++dropRequest;
                owner.dropFeedback(ru(u8"Получаю регион от Logic…"));
                return [safe = juce::Component::SafePointer<WavePanel>(this), context, request](flowalign::RegionDrop data)
                {
                    if (safe && request == safe->dropRequest) safe->owner.receiveDrop(std::move(data), context);
                };
            };
        }
        setTooltipText();
    }
    void setTooltipText()
    {
        source.setTooltip(track == Track::result ? ru(u8"Показать готовый WAV в Finder. Можно перетащить его из Finder в Logic без ручного сохранения.")
            : ru(u8"Записать этот сигнал с таймлайна: нажмите кнопку, затем Play в Logic. Для эталона выберите Side Chain. Файлы доступны в меню шестерёнки."));
    }
    void resized() override
    {
        const float scale = getWidth() / 1560.0f;
        source.setBounds(juce::roundToInt(24 * scale), juce::roundToInt((track == Track::reference ? 105 : 92) * scale),
                         juce::roundToInt(143 * scale), juce::roundToInt(32 * scale));
        audition.setBounds(juce::roundToInt(174 * scale), source.getY(), juce::roundToInt(70 * scale), source.getHeight());
        if (nativeDrop)
        {
            const int top = juce::roundToInt((track == Track::reference ? 32 : 12) * scale);
            nativeDrop->setBounds(juce::roundToInt(272 * scale), top, juce::roundToInt(1204 * scale),
                                  getHeight() - top - juce::roundToInt(12 * scale));
        }
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override { return track != Track::result && owner.isInterestedInFileDrag(files); }
    void fileDragEnter(const juce::StringArray&, int, int) override { hovering = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { hovering = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override
    { hovering = false; if (!files.isEmpty()) owner.engine.loadFile(juce::File(files[0]), track); repaint(); }
    void mouseDown(const juce::MouseEvent& event) override { seekAt(event.position.x); }
    void seekAt(float x)
    {
        const float scale = static_cast<float>(getWidth()) / 1560.0f;
        const double fraction = (x / scale - 272.0f) / 1204.0f;
        if (fraction >= 0.0 && fraction <= 1.0)
        {
            const auto s = owner.engine.snapshot();
            const auto clip = track == Track::reference ? s->reference : track == Track::dub ? s->dub : s->result;
            const double duration = juce::jmax(s->reference || s->dub ? 1.0 : 10.0,
                juce::jmax(s->reference ? s->reference->duration() : 0.0, s->dub ? s->dub->duration() : 0.0));
            const double length = duration / owner.zoom.getValue();
            const double start = juce::jlimit(0.0, juce::jmax(0.0, duration - length), owner.engine.playbackSeconds() - length * 0.5);
            if (clip) owner.engine.seek(start + fraction * length);
        }
    }
    void paint(juce::Graphics& graphics) override
    {
        const float scale = getWidth() / 1560.0f;
        juce::Graphics::ScopedSaveState saved(graphics); graphics.addTransform(juce::AffineTransform::scale(scale));
        auto& g = graphics;
        const float h = getHeight() / scale;
        panel(g, { 0.5f, 0.5f, 1559.0f, h - 1.0f });
        if (hovering) { g.setColour(green.withAlpha(0.14f)); g.fillRoundedRectangle(2, 2, 1556, h - 4, 14); }
        const auto session = owner.engine.snapshot();
        const auto clip = track == Track::reference ? session->reference : track == Track::dub ? session->dub : session->result;
        const bool ref = track == Track::reference, output = track == Track::result;
        const juce::Colour colour = ref ? juce::Colour(0xffa9bac7) : green;
        if (!output) microphone(g, 22, 42, ref ? white : green);
        else
        {
            g.setColour(green); g.drawEllipse(19, 48, 24, 24, 1.7f); g.drawEllipse(30, 37, 24, 24, 1.7f);
        }
        flowText(g, ref ? ru(u8"ОПОРНАЯ ДОРОЖКА") : output ? ru(u8"РЕЗУЛЬТАТ") : ru(u8"ДОП. ДОРОЖКА"), 19, { 62, 39, 205, 25 }, white, true);
        flowText(g, ref ? ru(u8"ОСНОВНОЙ ВОКАЛ") : output ? ru(u8"ВЫРОВНЕННЫЙ ВОКАЛ") : ru(u8"ВОКАЛ ДЛЯ ВЫРАВНИВАНИЯ"), 11.7f, { 62, 67, 200, 18 }, muted);
        const double timelineStart = ref ? session->referenceStartSeconds : session->anchorSeconds;
        const auto position = timelineStart >= 0.0 ? ru(u8"С ") + juce::String(timelineStart, 3) + ru(u8" с") : ru(u8"Позиция не задана");
        const auto info = clip ? position + "  /  " + juce::String(clip->duration(), 2) + ru(u8" с") : ref ? ru(u8"ИСТОЧНИК: SIDE CHAIN LOGIC") : ru(u8"ИСТОЧНИК: ЭТА ДОРОЖКА");
        flowText(g, info, 10.5f, { 24, ref ? 86 : 77, 236, 16 }, muted.withAlpha(0.85f));
        source.setButtonText(output ? ru(u8"WAV В FINDER") : ref ? (clip ? ru(u8"Перезаписать эталон") : ru(u8"Записать эталон")) : (clip ? ru(u8"Перезаписать дубль") : ru(u8"Записать дубль")));
        source.setEnabled(!owner.engine.isBusy() && !owner.engine.isCapturing() && (!output || clip != nullptr));
        const bool playingThis = owner.engine.isPreviewing() && owner.engine.previewTrack() == track;
        audition.setButtonText(playingThis ? ru(u8"Остановить") : ru(u8"Слушать"));
        audition.setToggleState(playingThis, juce::dontSendNotification);
        audition.setEnabled(clip != nullptr && !owner.engine.isBusy() && !owner.engine.isCapturing() && !owner.engine.isCapturePending());
        const juce::Rectangle<float> wave(272, ref ? 32.0f : 12.0f, 1204, h - (ref ? 44.0f : 24.0f));
        g.setColour(juce::Colour(0xff050c0e)); g.fillRoundedRectangle(wave, 4);
        g.setColour(juce::Colour(0xff172426));
        for (int i = 0; i <= 28; ++i) g.drawVerticalLine(static_cast<int>(wave.getX() + wave.getWidth() * i / 28), wave.getY(), wave.getBottom());
        g.drawHorizontalLine(static_cast<int>(wave.getCentreY()), wave.getX(), wave.getRight());
        const double duration = juce::jmax(session->reference || session->dub ? 1.0 : 10.0, juce::jmax(session->reference ? session->reference->duration() : 0.0,
                                                     session->dub ? session->dub->duration() : 0.0));
        const double zoom = owner.zoom.getValue();
        const double viewLength = duration / zoom;
        const double viewStart = juce::jlimit(0.0, juce::jmax(0.0, duration - viewLength), owner.engine.playbackSeconds() - viewLength * 0.5);
        if (ref)
            for (int i = 0; i <= 12; ++i)
                flowText(g, viewLength < 12.0 ? juce::String(viewStart + viewLength * i / 12.0, 1) + ru(u8" с") : timeText(viewStart + viewLength * i / 12.0), 10, { 272 + i * 100, 9, 70, 16 }, i == 0 ? muted : muted.withAlpha(0.7f));
        auto drawPeaks = [&](const std::vector<FLOWALIGNAudioProcessor::Peak>& peaks, juce::Colour ink, bool live, double clipDuration)
        {
            if (peaks.empty()) return;
            g.setColour(ink);
            for (int x = 0; x < static_cast<int>(wave.getWidth()); ++x)
            {
                const double a = live ? static_cast<double>(x) / wave.getWidth() : (viewStart + viewLength * x / wave.getWidth()) / clipDuration;
                const double b = live ? static_cast<double>(x + 1) / wave.getWidth() : (viewStart + viewLength * (x + 1) / wave.getWidth()) / clipDuration;
                if (a >= 1.0) continue;
                const auto start = juce::jlimit<size_t>(0, peaks.size() - 1, static_cast<size_t>(a * peaks.size()));
                const auto end = juce::jlimit<size_t>(start + 1, peaks.size(), static_cast<size_t>(b * peaks.size()) + 1);
                float low = 0.0f, high = 0.0f;
                for (auto i = start; i < end; ++i) { low = juce::jmin(low, peaks[i].low); high = juce::jmax(high, peaks[i].high); }
                const float mid = wave.getCentreY(), height = wave.getHeight() * 0.43f;
                g.drawVerticalLine(static_cast<int>(wave.getX()) + x, mid - juce::jlimit(0.0f, 1.0f, high) * height,
                                   mid - juce::jlimit(-1.0f, 0.0f, low) * height + 0.6f);
            }
        };
        if (clip && !owner.engine.isCapturingTrack(track))
        {
            if (output && session->reference) drawPeaks(session->reference->peaks, colour.withAlpha(0.17f), false, session->reference->duration());
            drawPeaks(clip->peaks, colour.withAlpha(0.85f), false, clip->duration());
        }
        else if (!output && owner.engine.isCapturingTrack(track)) drawPeaks(owner.engine.liveWaveform(ref), colour.withAlpha(0.7f), true, duration);
        if (!clip && !owner.engine.isCapturingTrack(track))
        {
            const auto hint = output ? ru(u8"Нажмите «Выровнять», чтобы создать результат и включить «После»")
                : ref ? ru(u8"Перетащите сюда регион из Logic или запишите эталон через Side Chain")
                      : ru(u8"Перетащите сюда регион той же длины или нажмите «Записать дубль»");
            auto hintBounds = wave.toNearestInt().withSizeKeepingCentre(juce::jmin(880, static_cast<int>(wave.getWidth()) - 40), 32);
            g.setColour(juce::Colour(0xe6050c0e)); g.fillRoundedRectangle(hintBounds.toFloat(), 8);
            flowText(g, hint, 14, hintBounds, muted.withAlpha(0.8f), false, juce::Justification::centred);
        }
        const double cursor = owner.engine.playbackSeconds();
        const float cursorX = wave.getX() + static_cast<float>(juce::jlimit(0.0, 1.0, (cursor - viewStart) / viewLength)) * wave.getWidth();
        g.setColour(green.withAlpha(0.12f)); g.fillRect(cursorX - 3, wave.getY(), 7.0f, wave.getHeight());
        g.setColour(green); g.drawLine(cursorX, wave.getY(), cursorX, wave.getBottom(), 1.4f);
        const float level = output ? owner.engine.outputLevel() : owner.engine.getPeak(ref);
        const float db = juce::Decibels::gainToDecibels(level, -60.0f);
        flowText(g, level < 0.00001f ? ru(u8"−∞ дБ") : juce::String(db, 1) + ru(u8" дБ"), 12, { 1486, 20, 66, 18 }, white, false, juce::Justification::centred);
        const juce::Rectangle<float> meter(1494, 44, 12, h - 58);
        g.setColour(juce::Colour(0xff172120)); g.fillRect(meter);
        auto fill = meter.withTop(meter.getBottom() - meter.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb6ff9b), fill.getX(), fill.getY(), green, fill.getX(), fill.getBottom(), false)); g.fillRect(fill);
        for (int d = 0; d <= 60; d += 12)
            flowText(g, d == 0 ? "0" : ru(u8"−") + juce::String(d), 8.5f,
                 { 1516, static_cast<int>(meter.getY() + meter.getHeight() * d / 60.0f) - 5, 36, 12 }, muted);
    }
private:
    FLOWALIGNAudioProcessorEditor& owner;
    Track track;
    juce::TextButton source, audition;
    std::unique_ptr<flowalign::NativeRegionDrop> nativeDrop;
    uint64_t dropRequest = 0;
    bool hovering = false;
};

class FLOWALIGNAudioProcessorEditor::AlignButton final : public juce::TextButton
{
public:
    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(11);
        for (int i = 8; i > 0; --i)
        { g.setColour(green.withAlpha(0.012f * (9 - i))); g.drawEllipse(r.expanded(static_cast<float>(i)), 2.0f); }
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b3930), r.getX(), r.getY(), juce::Colour(0xff03090a), r.getRight(), r.getBottom(), false));
        g.fillEllipse(r); g.setColour(green.withAlpha(isEnabled() ? 1.0f : 0.35f)); g.drawEllipse(r, over ? 3.5f : 2.5f);
        g.setColour(green.withAlpha(down ? 0.3f : 0.08f)); g.drawEllipse(r.reduced(5), 3);
        flowText(g, getButtonText(), juce::jmax(12.0f, getHeight() * 0.1f), r.toNearestInt().reduced(5), white, true, juce::Justification::centred);
    }
};

class FLOWALIGNAudioProcessorEditor::DragButton final : public juce::TextButton
{
public:
    std::function<void()> onDrag;
    void mouseDown(const juce::MouseEvent& event) override { dragged = false; juce::TextButton::mouseDown(event); }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (dragged) { setState(juce::Button::buttonNormal); return; }
        juce::TextButton::mouseUp(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragged && event.getDistanceFromDragStart() > 6 && onDrag) { dragged = true; onDrag(); }
        else juce::TextButton::mouseDrag(event);
    }
private:
    bool dragged = false;
};

FLOWALIGNAudioProcessorEditor::FLOWALIGNAudioProcessorEditor(FLOWALIGNAudioProcessor& p)
 : AudioProcessorEditor(&p), engine(p)
{
    setLookAndFeel(&look);
    look.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0c1517));
    look.setColour(juce::ComboBox::outlineColourId, border);
    look.setColour(juce::ComboBox::textColourId, white);
    look.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0b1416));
    look.setColour(juce::Slider::textBoxTextColourId, white);
    look.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    look.setColour(juce::ToggleButton::textColourId, white);
    look.setColour(juce::ToggleButton::tickColourId, green);
    referenceWave = std::make_unique<WavePanel>(*this, Track::reference);
    dubWave = std::make_unique<WavePanel>(*this, Track::dub);
    resultWave = std::make_unique<WavePanel>(*this, Track::result);
    align = std::make_unique<AlignButton>(); drag = std::make_unique<DragButton>();
    for (auto* wave : { referenceWave.get(), dubWave.get(), resultWave.get() }) addAndMakeVisible(wave);
    for (auto* component : std::initializer_list<juce::Component*> { align.get(), drag.get(), &capture, &play, &stop, &loop, &before, &after, &bypass,
        &link, &settingsButton, &presets, &previousPreset, &nextPreset, &profileA, &profileB, &localMode, &hostMode,
        &saveResult, &saveOriginal, &captureTarget, &pairs, &addPair, &tightness, &naturalness, &maxStretch, &zoom, &preservePitch, &protectTransients, &autoGain, &status }) addAndMakeVisible(component);
    capture.setButtonText(ru(u8"●  ЗАПИСЬ")); play.setButtonText(ru(u8"▶")); stop.setButtonText(ru(u8"■")); loop.setButtonText(ru(u8"↻"));
    before.setButtonText(ru(u8"ДО")); after.setButtonText(ru(u8"ПОСЛЕ")); bypass.setButtonText(ru(u8"ОБХОД"));
    link.setButtonText(ru(u8"↔")); settingsButton.setButtonText(ru(u8"⚙")); previousPreset.setButtonText(ru(u8"‹")); nextPreset.setButtonText(ru(u8"›"));
    play.setComponentID("icon_play"); stop.setComponentID("icon_stop"); loop.setComponentID("icon_loop");
    link.setComponentID("icon_link"); settingsButton.setComponentID("icon_settings");
    profileA.setButtonText("A"); profileB.setButtonText("B"); align->setButtonText(ru(u8"ВЫРОВНЯТЬ"));
    localMode.setButtonText(ru(u8"ПЛЕЕР: ВЫХОД macOS")); hostMode.setButtonText(ru(u8"ПО ТРАНСПОРТУ LOGIC"));
    saveResult.setButtonText(ru(u8"СОХРАНИТЬ РЕЗУЛЬТАТ WAV")); drag->setButtonText(ru(u8"ЗАЖМИТЕ И ТАЩИТЕ В LOGIC"));
    saveOriginal.setButtonText(ru(u8"ИСХОДНЫЙ ДУБЛЬ / КАРТА"));
    preservePitch.setButtonText(ru(u8"СОХРАНЯТЬ ВЫСОТУ ГОЛОСА")); protectTransients.setButtonText(ru(u8"ЗАЩИТА ТРАНЗИЕНТОВ")); autoGain.setButtonText(ru(u8"ВЫРОВНЯТЬ ГРОМКОСТЬ"));
    for (auto* button : std::initializer_list<juce::Button*> { &bypass, &loop, &preservePitch, &protectTransients, &autoGain }) button->setClickingTogglesState(true);
    presets.addItem(ru(u8"По умолчанию"), 1); presets.addItem(ru(u8"Плотный дубль"), 2); presets.addItem(ru(u8"Естественный вокал"), 3);
    presets.setSelectedId(1, juce::dontSendNotification); presets.setJustificationType(juce::Justification::centred);
    presets.onChange = [this] { applyPreset(presets.getSelectedId()); };
    previousPreset.onClick = [this] { presets.setSelectedId(presets.getSelectedId() == 1 ? 3 : presets.getSelectedId() - 1); };
    nextPreset.onClick = [this] { presets.setSelectedId(presets.getSelectedId() == 3 ? 1 : presets.getSelectedId() + 1); };
    profileA.onClick = [this] { selectProfile(0); }; profileB.onClick = [this] { selectProfile(1); };
    profiles[0] = profiles[1] = engine.settings();
    captureTarget.addItem(ru(u8"Запись: оба сигнала"), 1); captureTarget.addItem(ru(u8"Запись: эталон"), 2); captureTarget.addItem(ru(u8"Запись: дубль"), 3);
    captureTarget.setSelectedId(1, juce::dontSendNotification);
    captureTarget.setTooltip(ru(u8"Можно записать эталон и дубль с разных позиций. Второй сигнал сохраняется."));
    pairs.onChange = [this] { engine.selectPair(pairs.getSelectedId() - 1); updateControls(); };
    addPair.setButtonText(ru(u8"+ ФРАЗА")); addPair.onClick = [this] { engine.newPair(); updateControls(); };
    capture.onClick = [this]
    {
        if (engine.isCapturing()) engine.stopCapture();
        else engine.startCapture(static_cast<FLOWALIGNAudioProcessor::CaptureMode>(captureTarget.getSelectedId() - 1));
        updateControls();
    };
    play.onClick = [this]
    {
        const auto s = engine.snapshot();
        auto target = engine.previewTrack();
        if ((target == Track::reference && !s->reference) || (target == Track::dub && !s->dub) || (target == Track::result && !s->result))
            target = s->result ? Track::result : s->dub ? Track::dub : Track::reference;
        togglePreview(target);
    };
    stop.onClick = [this] { engine.stopCapture(); engine.stopPreview(); };
    align->onClick = [this] { if (engine.isBusy()) engine.cancelJob(); else engine.startAlignment(); updateControls(); };
    before.onClick = [this]
    {
        engine.setValue("after", 0);
        if (engine.isPreviewing()) engine.startSystemPreview(Track::dub, engine.playbackSeconds());
        else engine.selectPreviewTrack(Track::dub);
    };
    after.onClick = [this]
    {
        engine.setValue("after", 1);
        if (engine.isPreviewing()) engine.startSystemPreview(Track::result, engine.playbackSeconds());
        else engine.selectPreviewTrack(Track::result);
    };
    after.setComponentID("afterResult");
    localMode.onClick = [this] { engine.setValue("syncHost", 0); };
    hostMode.onClick = [this]
    {
        if (engine.snapshot()->anchorSeconds >= 0.0 || engine.anchorToHost()) engine.setValue("syncHost", 1);
    };
    link.onClick = [this] { if (engine.anchorToHost()) engine.setValue("syncHost", 1); };
    saveResult.onClick = [this] { exportAudio(Track::result); };
    drag->setComponentID("transferResult");
    drag->onDrag = [this] { dragResult(); }; drag->onClick = [this] { transferHint(); };
    drag->setTooltip(ru(u8"Зажмите левую кнопку мыши и перетащите эту кнопку в пустую область таймлайна Logic. Обычный клик показывает инструкцию; автоматическое создание дорожки не реализовано."));
    saveOriginal.onClick = [this]
    {
        juce::PopupMenu menu; menu.addItem(1, ru(u8"Сохранить исходный дубль WAV")); menu.addItem(2, ru(u8"Сохранить карту тайминга CSV"), engine.snapshot()->result != nullptr);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&saveOriginal), [safe = juce::Component::SafePointer<FLOWALIGNAudioProcessorEditor>(this)](int id)
        { if (safe) { if (id == 1) safe->exportAudio(Track::dub); if (id == 2) safe->saveTiming(); } });
    };
    settingsButton.onClick = [this]
    {
        juce::PopupMenu menu; menu.addItem(1, ru(u8"Как добавить дорожки и выровнять вокал"));
        menu.addItem(2, ru(u8"Очистить сессию…"), !engine.isBusy() && !engine.isCapturing()); menu.addItem(4, ru(u8"Открыть файл эталона…"), !engine.isBusy() && !engine.isCapturing());
        menu.addItem(5, ru(u8"Открыть файл дубля…"), !engine.isBusy() && !engine.isCapturing());
        menu.addSeparator(); menu.addItem(3, "FLOWALIGN 0.3.2 / FLOWMINNERS", false);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton), [safe = juce::Component::SafePointer<FLOWALIGNAudioProcessorEditor>(this)](int id)
        {
            if (!safe) return;
            if (id == 1) safe->showHelp();
            if (id == 4) safe->openAudio(Track::reference);
            if (id == 5) safe->openAudio(Track::dub);
            if (id == 2)
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, ru(u8"Очистить сессию?"),
                    ru(u8"Загруженные сигналы и результат будут удалены из плагина. Файлы на диске сохранятся."), ru(u8"Очистить"), ru(u8"Отмена"), safe.getComponent(),
                    juce::ModalCallbackFunction::create([safe](int result) { if (safe && result != 0) safe->engine.clearSession(); }));
        });
    };
    auto slider = [&](juce::Slider& control, const char* id)
    {
        control.setSliderStyle(juce::Slider::RotaryVerticalDrag); control.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 25);
        control.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        control.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
        control.valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };
        control.setDoubleClickReturnValue(true, engine.parameters.getParameter(id)->convertFrom0to1(engine.parameters.getParameter(id)->getDefaultValue()));
        sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(engine.parameters, id, control));
        control.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
        control.valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };
        control.updateText();
    };
    slider(tightness, "tightness"); slider(naturalness, "naturalness"); slider(maxStretch, "maxStretch");
    auto attach = [&](juce::Button& control, const char* id)
    { buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(engine.parameters, id, control)); };
    attach(bypass, "bypass"); attach(loop, "loop"); attach(preservePitch, "preservePitch"); attach(protectTransients, "protectTransients"); attach(autoGain, "autoGain");
    zoom.setSliderStyle(juce::Slider::LinearHorizontal); zoom.setRange(1, 8, 0.1); zoom.setValue(1);
    zoom.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); zoom.setColour(juce::Slider::trackColourId, green);
    zoom.setColour(juce::Slider::thumbColourId, green); zoom.onValueChange = [this] { repaint(); referenceWave->repaint(); dubWave->repaint(); resultWave->repaint(); };
    status.setColour(juce::Label::textColourId, muted); status.setFont(juce::FontOptions("Arial", 12.0f, juce::Font::plain));
    capture.setTooltip(ru(u8"Записать выбранные сигналы с таймлайна. Затем нажмите Play в Logic; Стоп завершает запись."));
    link.setTooltip(ru(u8"Привязать начало загруженных файлов к текущей позиции курсора Logic"));
    hostMode.setTooltip(ru(u8"Подставлять результат при воспроизведении Logic внутри захваченного диапазона"));
    localMode.setTooltip(ru(u8"Кнопки «Слушать» у каждой волны используют системный выход macOS. Работает при остановленном Logic и Mute дорожки; выход Logic может отличаться от системного."));
    align->setTooltip(ru(u8"Выравнивание тайминга по опорной дорожке. Нужны оба аудиосигнала."));
    tightness.setTooltip(ru(u8"Степень применения найденной карты тайминга. 0% сохраняет дубль."));
    naturalness.setTooltip(ru(u8"Сглаживание карты тайминга для сохранения естественной фразировки"));
    maxStretch.setTooltip(ru(u8"Ограничение локального сжатия и растяжения времени"));
    setResizable(true, true); setResizeLimits(1000, 615, 1920, 1180);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio(1600.0 / 980.0);
    setSize(1280, 784); updateControls(); startTimerHz(25);
}

FLOWALIGNAudioProcessorEditor::~FLOWALIGNAudioProcessorEditor()
{
    stopTimer(); engine.stopPreview(); chooser.reset(); setLookAndFeel(nullptr);
}

juce::Rectangle<int> FLOWALIGNAudioProcessorEditor::scaled(int x, int y, int w, int h) const
{
    return juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h))
        .transformedBy(juce::AffineTransform::scale(getWidth() / 1600.0f, getHeight() / 980.0f)).toNearestInt();
}

void FLOWALIGNAudioProcessorEditor::resized()
{
    referenceWave->setBounds(scaled(20, 108, 1560, 162)); dubWave->setBounds(scaled(20, 282, 1560, 142)); resultWave->setBounds(scaled(20, 436, 1560, 142));
    previousPreset.setBounds(scaled(536, 32, 40, 38)); presets.setBounds(scaled(580, 32, 258, 38)); nextPreset.setBounds(scaled(842, 32, 40, 38));
    profileA.setBounds(scaled(916, 32, 48, 38)); profileB.setBounds(scaled(968, 32, 48, 38)); link.setBounds(scaled(1034, 32, 48, 38));
    bypass.setBounds(scaled(1100, 32, 102, 38)); settingsButton.setBounds(scaled(1528, 32, 42, 38));
    tightness.setBounds(scaled(395, 654, 156, 154)); naturalness.setBounds(scaled(589, 654, 156, 154)); maxStretch.setBounds(scaled(783, 654, 156, 154));
    hostMode.setBounds(scaled(1008, 632, 252, 43)); localMode.setBounds(scaled(1008, 681, 252, 40));
    preservePitch.setBounds(scaled(1008, 741, 252, 28)); protectTransients.setBounds(scaled(1008, 775, 252, 28)); autoGain.setBounds(scaled(1008, 809, 252, 28));
    drag->setBounds(scaled(1310, 638, 252, 57)); saveResult.setBounds(scaled(1310, 704, 252, 57)); saveOriginal.setBounds(scaled(1310, 770, 252, 57));
    capture.setBounds(scaled(42, 899, 122, 55)); play.setBounds(scaled(176, 899, 75, 55)); stop.setBounds(scaled(263, 899, 75, 55)); loop.setBounds(scaled(350, 899, 75, 55));
    align->setBounds(scaled(722, 839, 156, 136)); before.setBounds(scaled(948, 902, 96, 48)); after.setBounds(scaled(1048, 902, 96, 48));
    zoom.setBounds(scaled(1204, 910, 266, 30)); status.setBounds(scaled(30, 858, 1540, 26));
    pairs.setBounds(scaled(536, 76, 214, 25)); addPair.setBounds(scaled(758, 76, 124, 25));
    captureTarget.setBounds(scaled(990, 76, 292, 25));
}

void FLOWALIGNAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::Graphics::ScopedSaveState save(graphics);
    graphics.addTransform(juce::AffineTransform::scale(getWidth() / 1600.0f, getHeight() / 980.0f));
    auto& g = graphics;
    g.fillAll(juce::Colour(0xff050a0b));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff122121), 800, -260, juce::Colour(0xff050a0b), 800, 650, true)); g.fillRect(0, 0, 1600, 980);
    g.setColour(border.withAlpha(0.6f)); g.drawRoundedRectangle(3, 3, 1594, 974, 17, 1);
    flowText(g, "FLOW", 48, { 39, 20, 168, 51 }, white, true); flowText(g, "ALIGN", 48, { 176, 20, 208, 51 }, green, true);
    font(g, 10, false, 0.35f); g.setColour(white); g.drawText(ru(u8"ВЫРАВНИВАНИЕ ВОКАЛЬНЫХ ДОРОЖЕК"), 42, 74, 460, 16, juce::Justification::centredLeft);
    font(g, 15, false, 0.4f); g.setColour(white); g.drawText("FLOWMINNERS", 1305, 33, 210, 24, juce::Justification::centred);
    flowText(g, ru(u8"ИНСТРУМЕНТЫ ДЛЯ ВОКАЛА"), 8, { 1298, 60, 227, 15 }, muted, false, juce::Justification::centred);
    panel(g, { 20, 595, 320, 257 }); panel(g, { 352, 595, 626, 257 }); panel(g, { 990, 595, 292, 257 }); panel(g, { 1294, 595, 286, 257 });
    flowText(g, ru(u8"ПРОЦЕСС ВЫРАВНИВАНИЯ"), 15, { 42, 608, 280, 25 }, white, true);
    flowText(g, ru(u8"НАСТРОЙКИ ВЫРАВНИВАНИЯ"), 15, { 374, 608, 580, 25 });
    flowText(g, ru(u8"РЕЖИМ ВОСПРОИЗВЕДЕНИЯ"), 14, { 1008, 608, 252, 22 }); flowText(g, ru(u8"ЭКСПОРТ / ПРИМЕНЕНИЕ"), 14, { 1310, 608, 252, 22 });
    const auto session = engine.snapshot();
    const float progress = engine.jobProgress();
    const bool ready = session->result != nullptr;
    const std::array<juce::String, 6> steps { ru(u8"Запись эталона и дубля"), ru(u8"Анализ опорной дорожки"), ru(u8"Анализ дополнительной"),
                                           ru(u8"Поиск совпадений"), ru(u8"Создание карты тайминга"), ru(u8"Применение изменений") };
    for (int i = 0; i < 6; ++i)
    {
        const bool complete = ready || (i == 0 && session->reference && session->dub)
            || (engine.isBusy() && engine.jobStage() == 2 && progress >= std::array<float, 6> { 0.0f, 0.06f, 0.12f, 0.48f, 0.5f, 1.0f }[static_cast<size_t>(i)]);
        const float y = static_cast<float>(650 + i * 28);
        g.setColour(complete ? green : muted.withAlpha(0.5f));
        if (complete)
        {
            g.fillEllipse(49, y, 19, 19); g.setColour(juce::Colour(0xff06381b));
            juce::Path tick; tick.startNewSubPath(54, y + 9); tick.lineTo(58, y + 13); tick.lineTo(64, y + 6); g.strokePath(tick, juce::PathStrokeType(2));
        }
        else g.drawEllipse(49, y, 19, 19, 1.3f);
        flowText(g, steps[static_cast<size_t>(i)], 13.5f, { 87, static_cast<int>(y), 236, 21 }, complete ? white : muted);
    }
    g.setColour(border); g.fillRoundedRectangle(40, 824, 278, 7, 3);
    if (engine.isBusy() || ready)
    { g.setColour(green); g.fillRoundedRectangle(40, 824, 278 * (ready && !engine.isBusy() ? 1.0f : progress), 7, 3); }
    const std::array<juce::String, 3> labels { ru(u8"ТОЧНОСТЬ"), ru(u8"ЕСТЕСТВЕННОСТЬ"), ru(u8"МАКС. РАСТЯЖЕНИЕ") };
    const std::array<juce::String, 3> hints { ru(u8"Следовать таймингу\nопорного вокала"), ru(u8"Сохранять живую\nфразировку"), ru(u8"Ограничить изменение\nдлительности") };
    for (int i = 0; i < 3; ++i)
    {
        flowText(g, labels[static_cast<size_t>(i)], 13, { 386 + i * 194, 641, 176, 22 }, white, true, juce::Justification::centred);
        font(g, 12.5f); g.setColour(muted); g.drawFittedText(hints[static_cast<size_t>(i)], { 390 + i * 194, 807, 170, 37 }, juce::Justification::centred, 2);
        if (i < 2) { g.setColour(border.withAlpha(0.5f)); g.drawVerticalLine(572 + i * 194, 634, 833); }
    }
    g.setColour(border.withAlpha(0.6f)); g.drawHorizontalLine(889, 22, 1578);
    panel(g, { 440, 899, 169, 55 }, 10);
    const auto previewClip = engine.previewTrack() == Track::reference ? session->reference
        : engine.previewTrack() == Track::result ? session->result : session->dub;
    const double duration = previewClip ? previewClip->duration() : 0.0;
    flowText(g, timeText(engine.playbackSeconds()) + " / " + timeText(duration), 15, { 448, 899, 153, 55 }, white, false, juce::Justification::centred);
    // Decorative interference lines follow the reference without imitating audio data.
    for (int i = 0; i < 4; ++i)
    {
        juce::Path line;
        for (int x = 616; x <= 936; ++x)
        {
            const float y = 925.0f + std::sin(x * 0.027f + i * 1.3f) * (9.0f + i * 3.0f);
            if (x == 616) line.startNewSubPath(static_cast<float>(x), y); else line.lineTo(static_cast<float>(x), y);
        }
        g.setColour(green.withAlpha(0.07f + i * 0.015f)); g.strokePath(line, juce::PathStrokeType(0.8f));
    }
    flowText(g, juce::String(juce::roundToInt(zoom.getValue() * 100)) + "%", 14, { 1490, 907, 75, 38 }, white, false, juce::Justification::centred);
}

bool FLOWALIGNAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return !engine.isBusy() && !engine.isCapturing() && !files.isEmpty()
        && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;mp3;m4a;ogg");
}

void FLOWALIGNAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int y)
{
    if (!files.isEmpty()) engine.loadFile(juce::File(files[0]), y < getHeight() * 0.28 ? Track::reference : Track::dub);
}

void FLOWALIGNAudioProcessorEditor::openAudio(Track track)
{
    chooser = std::make_unique<juce::FileChooser>(track == Track::reference ? ru(u8"Открыть опорный вокал") : ru(u8"Открыть дубль"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<FLOWALIGNAudioProcessorEditor>(this), track](const juce::FileChooser& dialog)
        { if (safe && dialog.getResult().existsAsFile()) safe->engine.loadFile(dialog.getResult(), track); });
}

void FLOWALIGNAudioProcessorEditor::sourceMenu(Track track, juce::Component* target)
{
    juce::PopupMenu menu; menu.addItem(1, ru(u8"Открыть аудиофайл…")); menu.addItem(2, ru(u8"Как захватить дорожку Logic Pro?"));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target), [safe = juce::Component::SafePointer<FLOWALIGNAudioProcessorEditor>(this), track](int result)
    { if (safe) { if (result == 1) safe->openAudio(track); if (result == 2) safe->showHelp(); } });
}

void FLOWALIGNAudioProcessorEditor::showHelp()
{
#if JUCE_WINDOWS
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, ru(u8"FLOWALIGN в Windows"),
        ru(u8"Перетащите два аудиофайла одинаковой длины на волны эталона и дубля. В FL Studio используйте ручку Drag/copy sample/selection в Edison: переносится выделение или весь сэмпл. Внутренний клип Playlist может не передавать аудиофайл. Тогда используйте захват через Side Chain.\n\n"
           u8"Вставьте FLOWALIGN на дубль. Направьте эталон в Side Chain и подключите дополнительный вход в настройках wrapper → Processing. Выберите «Оба сигнала», нажмите «Запись», затем Play в секвенсоре. Stop завершает захват. Эталон и дубль можно записать отдельно. Mute/Solo не выбирают источник.\n\n"
           u8"Кнопки «Слушать» воспроизводят каждый сигнал отдельно через системный выход Windows. Для этого остановите транспорт. Если устройство занято эксклюзивным ASIO, освободите системный выход или слушайте через транспорт.\n\n"
           u8"Нажмите «Выровнять», сравните «До / После» и перетащите кнопку результата на таймлайн. При недоступном переносе откройте готовый WAV кнопкой у результата. Обычный клик не создаёт дорожку.\n\n"
           u8"Позиция импортированного аудио не передаётся: для режима транспорта поставьте курсор на начало дубля и нажмите кнопку связи. Не запускайте секвенсор от администратора: это мешает переносу из Проводника.\n\n"
           u8"До 8 пар, до 120 секунд на сигнал, суммарно 256 МБ. После изменения настроек снова нажмите «Выровнять»."), ru(u8"Понятно"), this);
#else
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, ru(u8"Запись с таймлайна Logic"),
        ru(u8"Быстрый способ: обрежьте два аудиорегиона в Logic до одинаковой длины. Перетащите по одному прямо на волны «Опорная дорожка» и «Доп. дорожка». Logic передаст готовые файлы; записывать их не нужно. Позиция на таймлайне при переносе не передаётся — для режима по транспорту задайте её кнопкой связи.\n\n"
           u8"1. Вставьте FLOWALIGN на дорожку дубля. В Side Chain окна Logic выберите эталон.\n\n"
           u8"2. Mute/Solo не выбирают источник эталона. Оставьте дорожку с плагином включённой и выберите опорную именно в Side Chain окна Logic.\n\n"
           u8"Выберите «Оба сигнала», поставьте курсор, нажмите «Запись», затем Play в Logic. Стоп завершает захват.\n\n"
           u8"3. Для разных позиций запишите «Эталон», затем отдельно «Дубль». Второй сигнал сохраняется. «+ Фраза» добавляет новую пару.\n\n"
           u8"4. У каждой волны есть «Слушать»: отдельно эталон, дубль и результат через системный выход macOS. Для изолированного прослушивания остановите Logic.\n\n"
           u8"5. Нажмите «Выровнять». После появления третьей волны можно слушать результат и сравнивать «До / После». Изменение регуляторов не удаляет готовый результат; повторный расчёт применяет новые настройки.\n\n"
           u8"6. Зажмите кнопку «Тащите в Logic» и перетащите её в пустую область таймлайна. Обычный клик не создаёт дорожку. Если перетаскивание недоступно, нажмите «WAV в Finder» у результата. WAV готовится автоматически. Позиция дубля показана слева.\n\n"
           u8"Автоматические Play и создание дорожки кнопкой пока недоступны: Bridge требует проверки в Logic. Файловый импорт и экспорт доступны дополнительно.\n\n"
           u8"До 8 пар, до 120 секунд на захват, общий лимит аудио 256 МБ. При изменении настроек снова нажмите «Выровнять»."), ru(u8"Понятно"), this);
#endif
}

void FLOWALIGNAudioProcessorEditor::exportAudio(Track track)
{
    chooser = std::make_unique<juce::FileChooser>(ru(u8"Сохранить аудио WAV"), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile(track == Track::result ? "FLOWALIGN-result.wav" : "FLOWALIGN-original.wav"), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<FLOWALIGNAudioProcessorEditor>(this), track](const juce::FileChooser& dialog)
        {
            if (!safe || dialog.getResult() == juce::File()) return;
            const bool ok = safe->engine.exportWave(dialog.getResult().withFileExtension("wav"), track);
            juce::AlertWindow::showMessageBoxAsync(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                "FLOWALIGN", ok ? ru(u8"WAV сохранён. Перетащите файл на дорожку в Logic Pro.") : ru(u8"Не удалось сохранить WAV. Проверьте доступ к папке."), ru(u8"ОК"));
        });
}

void FLOWALIGNAudioProcessorEditor::saveTiming()
{
    chooser = std::make_unique<juce::FileChooser>(ru(u8"Сохранить карту тайминга"), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("FLOWALIGN-timing.csv"), "*.csv");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<FLOWALIGNAudioProcessorEditor>(this)](const juce::FileChooser& dialog)
        {
            if (!safe || dialog.getResult() == juce::File()) return;
            if (!safe->engine.exportTiming(dialog.getResult().withFileExtension("csv")))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "FLOWALIGN", ru(u8"Не удалось сохранить карту тайминга"), ru(u8"ОК"));
        });
}

void FLOWALIGNAudioProcessorEditor::togglePreview(Track track)
{
    feedback.clear();
    if (engine.isPreviewing() && engine.previewTrack() == track) engine.stopPreview();
    else engine.startSystemPreview(track);
    updateControls(); referenceWave->repaint(); dubWave->repaint(); resultWave->repaint();
}

void FLOWALIGNAudioProcessorEditor::transferHint()
{
    feedback = ru(u8"Зажмите кнопку и тащите её на таймлайн Logic. Для переноса через Finder нажмите «WAV в Finder» у результата");
    feedbackUntil = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    updateControls();
}

void FLOWALIGNAudioProcessorEditor::revealResult()
{
    feedback.clear();
    const auto file = engine.prepareResultForLogic();
    if (file.existsAsFile()) file.revealToUser();
    updateControls();
}

void FLOWALIGNAudioProcessorEditor::dragResult()
{
    feedback.clear();
    const auto file = engine.prepareResultForLogic();
    if (!file.existsAsFile()) { updateControls(); return; }
    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, drag.get());
    feedback = started ? ru(u8"Перетаскивание начато. Отпустите файл над нужной областью таймлайна Logic")
        : ru(u8"Перетаскивание не запустилось. Нажмите «WAV в Finder» у результата и перенесите файл оттуда");
    feedbackUntil = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    updateControls();
}

void FLOWALIGNAudioProcessorEditor::applyPreset(int preset)
{
    const std::array<flowalign::Settings, 3> values { flowalign::Settings { 0.7f, 0.6f, 0.3f, true, true, false },
        flowalign::Settings { 0.95f, 0.25f, 0.5f, true, true, false }, flowalign::Settings { 0.5f, 0.85f, 0.2f, true, true, false } };
    const auto& s = values[static_cast<size_t>(juce::jlimit(1, 3, preset) - 1)];
    engine.setValue("tightness", s.tightness); engine.setValue("naturalness", s.naturalness); engine.setValue("maxStretch", s.maxStretch);
    engine.setValue("preservePitch", s.preservePitch ? 1 : 0); engine.setValue("protectTransients", s.protectTransients ? 1 : 0); engine.setValue("autoGain", s.autoGain ? 1 : 0);
}

void FLOWALIGNAudioProcessorEditor::selectProfile(int profile)
{
    if (profile == activeProfile) return;
    profiles[static_cast<size_t>(activeProfile)] = engine.settings(); activeProfile = profile;
    const auto& s = profiles[static_cast<size_t>(profile)];
    engine.setValue("tightness", s.tightness); engine.setValue("naturalness", s.naturalness); engine.setValue("maxStretch", s.maxStretch);
    engine.setValue("preservePitch", s.preservePitch ? 1 : 0); engine.setValue("protectTransients", s.protectTransients ? 1 : 0); engine.setValue("autoGain", s.autoGain ? 1 : 0);
}

void FLOWALIGNAudioProcessorEditor::updateControls()
{
    const auto session = engine.snapshot();
    const bool busy = engine.isBusy(), capturing = engine.isCapturing(), pending = engine.isCapturePending(), result = session->result != nullptr;
    if (pairRevision != session->revision)
    {
        pairRevision = session->revision;
        pairs.clear(juce::dontSendNotification); pairs.addItemList(engine.pairNames(), 1);
        pairs.setSelectedId(session->id + 1, juce::dontSendNotification);
    }
    pairs.setEnabled(!busy && !capturing && !pending); addPair.setEnabled(!busy && !capturing && !pending && session->otherPairs.size() < 7);
    captureTarget.setEnabled(!busy && !capturing && !pending);
    capture.setEnabled(!busy && !pending); capture.setToggleState(capturing, juce::dontSendNotification);
    capture.setButtonText(capturing ? ru(u8"■  ЗАВЕРШИТЬ") : ru(u8"●  ЗАПИСЬ"));
    play.setEnabled((session->reference || session->dub || session->result) && !capturing && !pending && !busy); play.setToggleState(engine.isPreviewing(), juce::dontSendNotification);
    align->setEnabled(!capturing && !pending && (busy || (session->reference && session->dub)));
    align->setButtonText(busy ? ru(u8"ОТМЕНА") : ru(u8"ВЫРОВНЯТЬ"));
    after.setEnabled(result && !capturing && !pending && !busy); before.setEnabled(session->dub != nullptr);
    after.setTooltip(result ? ru(u8"Слушать последний рассчитанный результат. Для новых настроек снова нажмите «Выровнять»")
        : ru(u8"Сначала нажмите «Выровнять» и дождитесь третьей осциллограммы"));
    const bool referencePlaying = engine.isPreviewing() && engine.previewTrack() == Track::reference;
    before.setToggleState(!referencePlaying && session->dub && (!result || engine.value("after") < 0.5f), juce::dontSendNotification);
    after.setToggleState(!referencePlaying && result && engine.value("after") > 0.5f, juce::dontSendNotification);
    hostMode.setToggleState(engine.value("syncHost") > 0.5f, juce::dontSendNotification);
    localMode.setToggleState(engine.value("syncHost") < 0.5f, juce::dontSendNotification);
    link.setEnabled(!busy && !capturing); link.setToggleState(session->anchorSeconds >= 0.0, juce::dontSendNotification);
    saveResult.setEnabled(result && !busy && !capturing && !pending); drag->setEnabled(result && !busy && !capturing && !pending); saveOriginal.setEnabled(session->dub != nullptr && !busy);
    profileA.setToggleState(activeProfile == 0, juce::dontSendNotification); profileB.setToggleState(activeProfile == 1, juce::dontSendNotification);
    auto line = engine.statusText();
    if (!engine.isPreviewing() && line == ru(u8"Прослушивание через аудиовыход плагина"))
        line = result ? ru(u8"Результат готов. Сравните «До / После» или перетащите его в Logic") : ru(u8"Дубль загружен. Добавьте опорный вокал для выравнивания");
    if (result && !engine.hasCurrentResult() && !busy && !capturing
        && !line.startsWith(ru(u8"Не удалось")))
        line = ru(u8"Настройки изменены. Доступен предыдущий результат; нажмите «Выровнять», чтобы применить новые настройки");
    if (pending) line = ru(u8"Завершение записи…");
    else if (capturing)
    {
        const bool needsReference = engine.captureMode() != FLOWALIGNAudioProcessor::CaptureMode::dub;
        line = engine.isCaptureRecording() ? ru(u8"Идёт запись: ") + timeText(engine.capturedSamples() / engine.captureSampleRate())
            : ru(u8"Нажмите Play в Logic. Дорожка с плагином должна оставаться включённой");
        if (needsReference && engine.referenceMirrorsDub())
            line = ru(u8"Вход эталона совпадает с дублем. Проверьте Side Chain вверху окна Logic; Mute не выбирает эталон");
        else if (needsReference && !engine.hasSidechain()) line += ru(u8" · эталон не подключён: выберите Side Chain в Logic");
        else if (engine.isCaptureRecording()) line += needsReference ? ru(u8" · эталон из Side Chain") : ru(u8" · дубль с этой дорожки");
    }
    else if (busy) line += "  " + juce::String(juce::roundToInt(engine.jobProgress() * 100)) + "%";
    else if (result) line += ru(u8"  ·  Средняя коррекция: ") + juce::String(session->correctionMs, 1) + ru(u8" мс");
    if (feedback.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < feedbackUntil && !capturing && !busy) line = feedback;
    status.setText(line, juce::dontSendNotification);
    status.setTooltip(line);
}

void FLOWALIGNAudioProcessorEditor::dropFeedback(juce::String message)
{
    feedback = std::move(message);
    feedbackUntil = juce::Time::getMillisecondCounterHiRes() + 10000.0;
}

void FLOWALIGNAudioProcessorEditor::receiveDrop(flowalign::RegionDrop data, FLOWALIGNAudioProcessor::DropContext context)
{
    if (data.error.isNotEmpty()) { dropFeedback(data.error); return; }
    if (!engine.isDropContextCurrent(context))
    { dropFeedback(ru(u8"Перенос отменён: фраза или выбранный сигнал уже изменились")); return; }
    if (pendingDrops.size() >= 8)
    { dropFeedback(ru(u8"Дождитесь загрузки уже перенесённых регионов")); return; }
    pendingDrops.push_back({ std::move(data), std::move(context) });
    pollDrops();
}

void FLOWALIGNAudioProcessorEditor::pollDrops()
{
    if (pendingDrops.empty() || engine.isBusy() || engine.isCapturing() || engine.isCapturePending()) return;
    auto pending = std::move(pendingDrops.front()); pendingDrops.pop_front();
    if (!engine.isDropContextCurrent(pending.context))
    { dropFeedback(ru(u8"Перенос отменён: фраза или выбранный сигнал уже изменились")); return; }
    feedback.clear(); feedbackUntil = 0.0;
    const bool started = (pending.data.promised || pending.data.checkLength)
        ? engine.loadDroppedRegion(pending.data.file, pending.context, std::move(pending.data.lease))
        : engine.loadFile(pending.data.file, pending.context.track);
    if (!started) dropFeedback(engine.statusText());
}

void FLOWALIGNAudioProcessorEditor::timerCallback()
{
    engine.pollCapture(); engine.pollPreview(); pollDrops(); updateControls();
    const auto session = engine.snapshot();
    if (session->revision != lastRevision) { lastRevision = session->revision; repaint(); }
    referenceWave->repaint(); dubWave->repaint(); resultWave->repaint();
    repaint(scaled(20, 595, 320, 257)); repaint(scaled(440, 899, 169, 55));
}
