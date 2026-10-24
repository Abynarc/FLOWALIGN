#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <stdexcept>
#if JUCE_MAC
 #include <pthread/qos.h>
#endif

namespace
{
juce::String ru(const char* s) { return juce::String::fromUTF8(s); }
struct ReaderEpoch
{
    std::atomic<int>& readers;
    explicit ReaderEpoch(std::atomic<int>& r) : readers(r) { readers.fetch_add(1); }
    ~ReaderEpoch() { readers.fetch_sub(1); }
};
}

juce::AudioProcessorValueTreeState::ParameterLayout FLOWALIGNAudioProcessor::parameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto number = [&](const char* id, const char* name, float low, float high, float initial)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), ru(name),
                   juce::NormalisableRange<float>(low, high, 0.01f), initial));
    };
    auto toggle = [&](const char* id, const char* name, bool initial)
    { layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), ru(name), initial)); };
    number("tightness", u8"Точность", 0.0f, 1.0f, 0.7f);
    number("naturalness", u8"Естественность", 0.0f, 1.0f, 0.6f);
    number("maxStretch", u8"Макс. растяжение", 0.05f, 0.8f, 0.3f);
    toggle("preservePitch", u8"Сохранять высоту голоса", true);
    toggle("protectTransients", u8"Защита транзиентов", true);
    toggle("autoGain", u8"Автоматическое усиление", false);
    toggle("after", u8"После", true);
    toggle("bypass", u8"Обход", false);
    toggle("loop", u8"Повтор", false);
    toggle("syncHost", u8"Воспроизведение по транспорту", false);
    return layout;
}

FLOWALIGNAudioProcessor::FLOWALIGNAudioProcessor()
 : AudioProcessor(BusesProperties()
       .withInput(ru(u8"Дубль"), juce::AudioChannelSet::stereo(), true)
       .withInput(ru(u8"Опорный вокал"), juce::AudioChannelSet::stereo(), false)
       .withOutput(ru(u8"Выход"), juce::AudioChannelSet::stereo(), true)),
   parameters(*this, nullptr, "FLOWALIGN", parameterLayout()),
   status(ru(u8"Выберите эталон в Side Chain, режим записи и нажмите «Запись», затем Play в Logic"))
{
    rawAfter = parameters.getRawParameterValue("after");
    rawBypass = parameters.getRawParameterValue("bypass");
    rawLoop = parameters.getRawParameterValue("loop");
    rawSync = parameters.getRawParameterValue("syncHost");
    publish(std::make_shared<Session>());
}

FLOWALIGNAudioProcessor::~FLOWALIGNAudioProcessor()
{
    cancelled.store(true);
    const std::lock_guard<std::mutex> lock(jobMutex);
    if (worker.joinable()) worker.join();
}

bool FLOWALIGNAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto main = layout.getMainInputChannelSet();
    if (main != layout.getMainOutputChannelSet()) return false;
    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo()) return false;
    const auto side = layout.inputBuses.size() > 1 ? layout.getChannelSet(true, 1) : juce::AudioChannelSet::disabled();
    return side.isDisabled() || side == juce::AudioChannelSet::mono() || side == juce::AudioChannelSet::stereo();
}

void FLOWALIGNAudioProcessor::prepareToPlay(double rate, int)
{
    const std::lock_guard<std::mutex> lock(captureMutex);
    captureRequested.store(false); capturePending.store(false); recording.store(false);
    previewing.store(false); hostPositionKnown.store(false); hostPlaying.store(false);
    captureReferenceMirrorsDub.store(false);
    captureRate.store(rate);
    // File alignment does not need recording storage. Allocate it only when
    // the user explicitly starts a capture, outside the audio callback.
    captureDub.setSize(0, 0); captureReference.setSize(0, 0);
    capturedCount.store(0); capturePosition = 0;
    referencePeak.store(0); dubPeak.store(0);
    meterSamples = 0; accumulatedReference = accumulatedDub = 0.0f;
    for (auto& v : liveReference) v.store(0.0f);
    for (auto& v : liveDub) v.store(0.0f);
    historyCursor.store(0);
}

void FLOWALIGNAudioProcessor::releaseResources()
{
    stopCapture();
    // Host suspension/muting must not stop the independent system audition.
    previewing.store(false); previewCursor.store(0.0); playbackPosition.store(0.0);
    outputPeak.store(0.0f);
}

void FLOWALIGNAudioProcessor::setValue(const char* id, float actual)
{
    if (auto* parameter = parameters.getParameter(id))
    {
        if (std::abs(parameter->convertFrom0to1(parameter->getValue()) - actual) < 0.000001f) return;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(actual));
        parameter->endChangeGesture();
    }
}

flowalign::Settings FLOWALIGNAudioProcessor::settings() const
{
    return { value("tightness"), value("naturalness"), value("maxStretch"),
             value("preservePitch") > 0.5f, value("protectTransients") > 0.5f, value("autoGain") > 0.5f };
}

float FLOWALIGNAudioProcessor::readSample(const Clip& clip, int channel, double seconds)
{
    const double position = seconds * clip.sampleRate;
    if (position < 0.0 || position >= clip.audio.getNumSamples()) return 0.0f;
    const int first = static_cast<int>(position), next = juce::jmin(first + 1, clip.audio.getNumSamples() - 1);
    const int c = juce::jmin(channel, clip.audio.getNumChannels() - 1);
    const float alpha = static_cast<float>(position - first);
    return clip.audio.getSample(c, first) * (1.0f - alpha) + clip.audio.getSample(c, next) * alpha;
}

void FLOWALIGNAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    ReaderEpoch epoch(audioReaders);
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer(buffer, true, 0);
    const bool hasSide = getBusCount(true) > 1 && getBus(true, 1)->isEnabled();
    auto side = hasSide ? getBusBuffer(buffer, true, 1) : juce::AudioBuffer<float>();
    sidechainPresent.store(hasSide);
    const int frames = main.getNumSamples();
    const double rate = captureRate.load();
    bool playing = false, havePosition = false;
    int64_t hostSample = 0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            playing = position->getIsPlaying();
            if (auto time = position->getTimeInSamples()) { hostSample = *time; havePosition = true; }
            else if (auto time = position->getTimeInSeconds())
            { hostSample = static_cast<int64_t>(std::llround(*time * rate)); havePosition = true; }
        }
    hostPlaying.store(playing); hostPositionKnown.store(havePosition);
    if (havePosition) hostSeconds.store(static_cast<double>(hostSample) / rate);

    float dp = 0.0f, rp = 0.0f;
    for (int c = 0; c < main.getNumChannels(); ++c) dp = juce::jmax(dp, main.getMagnitude(c, 0, frames));
    if (hasSide) for (int c = 0; c < side.getNumChannels(); ++c) rp = juce::jmax(rp, side.getMagnitude(c, 0, frames));
    dubPeak.store(juce::jmax(dp, dubPeak.load() * 0.93f));
    referencePeak.store(juce::jmax(rp, referencePeak.load() * 0.93f));
    accumulatedDub = juce::jmax(accumulatedDub, dp); accumulatedReference = juce::jmax(accumulatedReference, rp);
    meterSamples += frames;
    if (meterSamples >= static_cast<int>(rate * 0.02))
    {
        const auto cursor = historyCursor.load();
        liveDub[cursor % historySize].store(accumulatedDub);
        liveReference[cursor % historySize].store(accumulatedReference);
        historyCursor.store(cursor + 1);
        meterSamples = 0; accumulatedDub = accumulatedReference = 0.0f;
    }

    if (captureRequested.load())
    {
        if (activeCaptureGeneration != captureGeneration.load())
        {
            activeCaptureGeneration = captureGeneration.load();
            capturePosition = 0; recording.store(false); captureHasHostTime = false;
        }
        if (recording.load() && havePosition && (!playing || (captureHasHostTime && std::abs(hostSample - expectedHostSample) > frames * 2)))
        { captureRequested.store(false); capturePending.store(true); recording.store(false); }
        else if (!havePosition || playing)
        {
            if (!recording.load())
            {
                captureStartSeconds = havePosition ? static_cast<double>(hostSample) / rate : -1.0;
                captureHasHostTime = havePosition;
                recording.store(true);
            }
            const int capacity = captureMode() == CaptureMode::reference ? captureReference.getNumSamples() : captureDub.getNumSamples();
            const int count = juce::jmax(0, juce::jmin(frames, capacity - capturePosition));
            if (captureMode() != CaptureMode::reference)
                for (int c = 0; c < captureDub.getNumChannels(); ++c)
                    captureDub.copyFrom(c, capturePosition, main, c, 0, count);
            if (captureMode() != CaptureMode::dub)
                for (int i = 0; i < count; ++i)
                {
                    float ref = 0.0f;
                    if (hasSide)
                        for (int c = 0; c < side.getNumChannels(); ++c)
                            ref += side.getSample(c, i) / static_cast<float>(side.getNumChannels());
                    if (hasSide)
                    {
                        float dubMono = 0.0f;
                        for (int c = 0; c < main.getNumChannels(); ++c)
                            dubMono += main.getSample(c, i) / static_cast<float>(main.getNumChannels());
                        // Compare the actual analysis signals, not waveform shapes,
                        // peaks or correlation. Silent samples cannot establish routing.
                        if (std::abs(ref) > 0.00001f || std::abs(dubMono) > 0.00001f)
                        {
                            captureComparedReference = true;
                            if (ref != dubMono) captureReferenceDifferent = true;
                        }
                    }
                    captureReference.setSample(0, capturePosition + i, ref);
                }
            captureReferenceMirrorsDub.store(captureComparedReference && !captureReferenceDifferent);
            capturePosition += count;
            capturedCount.store(capturePosition);
            expectedHostSample = hostSample + frames;
            if (capturePosition >= capacity)
            { captureRequested.store(false); capturePending.store(true); recording.store(false); }
        }
    }
    const auto* session = realtimeSession.load();
    if (session == nullptr || rawBypass->load() > 0.5f || captureRequested.load() || capturePending.load())
    {
        outputPeak.store(juce::jmax(dp, outputPeak.load() * 0.93f));
        return;
    }
    const bool after = rawAfter->load() > 0.5f && session->result != nullptr;
    const auto previewTarget = previewTrack();
    const auto* clip = clipFor(*session, previewTarget);
    if (previewing.load() && clip != nullptr)
    {
        const auto seekAtStart = seekGeneration.load();
        double cursor = previewCursor.load();
        bool ended = false;
        for (int i = 0; i < frames; ++i)
        {
            if (cursor >= clip->duration())
            {
                if (rawLoop->load() > 0.5f) cursor = std::fmod(cursor, clip->duration());
                else ended = true;
            }
            for (int c = 0; c < main.getNumChannels(); ++c)
                main.setSample(c, i, ended ? 0.0f : readSample(*clip, c, cursor));
            if (!ended) cursor += 1.0 / rate;
        }
        if (seekGeneration.load() == seekAtStart) { previewCursor.store(cursor); playbackPosition.store(cursor); }
        if (ended) previewing.store(false);
    }
    else if (rawSync->load() > 0.5f && after && session->result && playing && havePosition && session->anchorSeconds >= 0.0)
    {
        const double offset = static_cast<double>(hostSample) / rate - session->anchorSeconds;
        for (int i = 0; i < frames; ++i)
        {
            const double time = offset + i / rate;
            if (time >= 0.0 && time < session->result->duration())
                for (int c = 0; c < main.getNumChannels(); ++c)
                    main.setSample(c, i, readSample(*session->result, c, time));
        }
        playbackPosition.store(juce::jmax(0.0, offset));
    }
    float output = 0.0f;
    for (int c = 0; c < main.getNumChannels(); ++c) output = juce::jmax(output, main.getMagnitude(c, 0, frames));
    outputPeak.store(juce::jmax(output, outputPeak.load() * 0.93f));
}

std::shared_ptr<const FLOWALIGNAudioProcessor::Session> FLOWALIGNAudioProcessor::snapshot() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return current;
}

void FLOWALIGNAudioProcessor::publish(std::shared_ptr<Session> session)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    session->revision = current ? current->revision + 1 : 1;
    if (current) retired.push_back(current);
    current = std::move(session);
    realtimeSession.store(current.get());
    if (audioReaders.load() == 0) retired.clear();
}

juce::String FLOWALIGNAudioProcessor::trackName() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return hostTrackName;
}

void FLOWALIGNAudioProcessor::updateTrackProperties(const TrackProperties& properties)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    hostTrackName = properties.name.value_or(juce::String());
}

juce::String FLOWALIGNAudioProcessor::statusText() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return status;
}

void FLOWALIGNAudioProcessor::setStatus(juce::String text)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    status = std::move(text);
}

std::shared_ptr<FLOWALIGNAudioProcessor::Clip> FLOWALIGNAudioProcessor::makeClip(juce::AudioBuffer<float> audio, double rate, juce::String name)
{
    auto clip = std::make_shared<Clip>();
    clip->audio = std::move(audio); clip->sampleRate = rate; clip->name = std::move(name);
    const int bins = juce::jmin(8192, clip->audio.getNumSamples());
    clip->peaks.resize(static_cast<size_t>(bins));
    for (int p = 0; p < bins; ++p)
    {
        const int start = static_cast<int>(static_cast<int64_t>(p) * clip->audio.getNumSamples() / bins);
        const int end = static_cast<int>(static_cast<int64_t>(p + 1) * clip->audio.getNumSamples() / bins);
        auto& peak = clip->peaks[static_cast<size_t>(p)];
        for (int c = 0; c < clip->audio.getNumChannels(); ++c)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(clip->audio.getReadPointer(c, start), end - start);
            peak.low = juce::jmin(peak.low, range.getStart()); peak.high = juce::jmax(peak.high, range.getEnd());
        }
    }
    return clip;
}

bool FLOWALIGNAudioProcessor::isCapturingTrack(Track track) const
{
    if (!isCapturing() || track == Track::result) return false;
    return captureMode() == CaptureMode::both
        || (track == Track::reference ? captureMode() == CaptureMode::reference : captureMode() == CaptureMode::dub);
}

bool FLOWALIGNAudioProcessor::startCapture(CaptureMode mode)
{
    const std::lock_guard<std::mutex> lock(captureMutex);
    if (mode != CaptureMode::both && mode != CaptureMode::reference && mode != CaptureMode::dub) return false;
    if (busy.load() || captureRequested.load() || capturePending.load()) return false;
    if (mode == CaptureMode::reference && (getBusCount(true) < 2 || !getBus(true, 1)->isEnabled()))
    {
        setStatus(ru(u8"Эталон не подключён. В Side Chain вверху окна Logic выберите опорную дорожку"));
        return false;
    }
    if (audioBytes(*snapshot()) > 192u * 1024u * 1024u)
    { setStatus(ru(u8"Недостаточно памяти для нового захвата. Сохраните проект и очистите сессию")); return false; }
    try
    {
        const auto bytesAvailable = 256u * 1024u * 1024u - audioBytes(*snapshot());
        const int channels = (mode == CaptureMode::reference ? 0 : getMainBusNumInputChannels()) + (mode == CaptureMode::dub ? 0 : 1);
        const int capacity = juce::jmin(static_cast<int>(std::ceil(captureRate.load() * 120.0)),
            static_cast<int>(bytesAvailable / (static_cast<size_t>(channels) * sizeof(float))));
        captureDub.setSize(mode == CaptureMode::reference ? 0 : getMainBusNumInputChannels(), mode == CaptureMode::reference ? 0 : capacity);
        captureReference.setSize(mode == CaptureMode::dub ? 0 : 1, mode == CaptureMode::dub ? 0 : capacity);
    }
    catch (const std::bad_alloc&)
    { setStatus(ru(u8"Недостаточно памяти для захвата")); return false; }
    dropEpoch.fetch_add(1);
    stopPreview(); capturedCount.store(0); recording.store(false);
    captureStartSeconds = -1.0;
    captureComparedReference = false; captureReferenceDifferent = false; captureReferenceMirrorsDub.store(false);
    requestedCaptureMode.store(static_cast<int>(mode));
    captureGeneration.fetch_add(1); captureRequested.store(true);
    setStatus(mode == CaptureMode::reference
        ? ru(u8"Запись эталона: источник задаётся в Side Chain окна Logic. Нажмите Play")
        : ru(u8"Запись готова: включите Play в Logic Pro с нужной позиции"));
    return true;
}

void FLOWALIGNAudioProcessor::stopCapture()
{
    if (captureRequested.exchange(false)) capturePending.store(true);
    recording.store(false);
}

void FLOWALIGNAudioProcessor::pollCapture()
{
    const std::lock_guard<std::mutex> lock(captureMutex);
    if (!capturePending.load() || captureRequested.load() || audioReaders.load() != 0) return;
    const int count = capturedCount.load();
    if (count > 0)
    {
        auto next = std::make_shared<Session>(*snapshot());
        if (captureMode() != CaptureMode::reference)
        {
            juce::AudioBuffer<float> dub(captureDub.getNumChannels(), count);
            for (int c = 0; c < dub.getNumChannels(); ++c) dub.copyFrom(c, 0, captureDub, c, 0, count);
            next->dub = makeClip(std::move(dub), captureRate.load(), trackName().isEmpty() ? ru(u8"Дубль · дорожка Logic") : trackName());
            next->anchorSeconds = captureStartSeconds;
        }
        if (captureMode() != CaptureMode::dub)
        {
            juce::AudioBuffer<float> ref(1, count);
            ref.copyFrom(0, 0, captureReference, 0, 0, count);
            // A failed new reference must never silently reuse the previous one.
            next->reference.reset();
            if (ref.getMagnitude(0, count) > 0.00001f && !captureReferenceMirrorsDub.load())
                next->reference = makeClip(std::move(ref), captureRate.load(), ru(u8"Эталон · Side Chain"));
            next->referenceStartSeconds = captureStartSeconds;
        }
        next->result.reset(); next->resultId.clear(); next->sourceSeconds.clear(); next->hasRenderedSettings = false;
        const bool ready = next->reference && next->dub;
        const bool missingReference = !next->reference;
        publish(std::move(next));
        if (captureMode() != CaptureMode::reference && captureStartSeconds >= 0.0) setValue("syncHost", 1);
        const bool mirroredReference = captureMode() != CaptureMode::dub && captureReferenceMirrorsDub.load();
        setStatus(mirroredReference ? ru(u8"Эталон отклонён: вход Side Chain совпадает с дублем. Выберите опорную дорожку в Side Chain окна Logic; Mute не меняет источник")
            : ready ? ru(u8"Запись завершена. Нажмите «Выровнять»")
            : missingReference ? ru(u8"Эталон не записан. Выберите дорожку в Side Chain и запишите «Эталон»")
                               : ru(u8"Эталон записан. Поставьте курсор на дубль и запишите «Дубль»"));
    }
    else setStatus(ru(u8"Звук не записан. Выберите позицию, нажмите «Запись», затем Play в Logic"));
    capturePending.store(false);
}

std::vector<FLOWALIGNAudioProcessor::Peak> FLOWALIGNAudioProcessor::liveWaveform(bool reference) const
{
    std::vector<Peak> result(historySize);
    const auto cursor = historyCursor.load();
    const auto& history = reference ? liveReference : liveDub;
    for (unsigned i = 0; i < historySize; ++i)
    {
        const float v = history[(cursor + i) % historySize].load();
        result[i] = { -v, v };
    }
    return result;
}

bool FLOWALIGNAudioProcessor::launch(std::function<void()> job)
{
    const std::lock_guard<std::mutex> lock(jobMutex);
    if (captureRequested.load() || capturePending.load() || busy.exchange(true)) return false;
    if (worker.joinable()) worker.join();
    cancelled.store(false); progress.store(0.0f);
    worker = std::thread([this, job = std::move(job)]
    {
       #if JUCE_MAC
        // Offline analysis must yield CPU priority to Logic's audio engine.
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
       #endif
        try { job(); }
        catch (const std::exception& error)
        {
            const auto reason = juce::String::fromUTF8(error.what());
            if (reason == "cancelled") setStatus(ru(u8"Обработка отменена. Исходное аудио сохранено"));
            else if (reason == "silent") setStatus(ru(u8"В одном из сигналов тишина. Проверьте вход и Side Chain"));
            else if (reason == "length") setStatus(ru(u8"Для выравнивания нужны сигналы от 0,1 до 120 секунд"));
            else if (reason == "invalid" || reason == "path") setStatus(ru(u8"Не удалось построить карту. Проверьте исходные сигналы"));
            else setStatus(ru(u8"Не удалось обработать аудио: ") + reason);
        }
        stage.store(0); busy.store(false);
    });
    return true;
}

FLOWALIGNAudioProcessor::DropContext FLOWALIGNAudioProcessor::dropContext(Track track) const
{
    const auto s = snapshot();
    return { track, s->id, dropEpoch.load(), track == Track::reference ? s->reference : s->dub };
}

bool FLOWALIGNAudioProcessor::isDropContextCurrent(const DropContext& context) const
{
    const auto s = snapshot();
    return context.track != Track::result && context.epoch == dropEpoch.load() && context.pairId == s->id
        && context.previous == (context.track == Track::reference ? s->reference : s->dub);
}

bool FLOWALIGNAudioProcessor::loadDroppedRegion(const juce::File& file, const DropContext& context, std::shared_ptr<void> lease)
{
    if (!isDropContextCurrent(context))
    { setStatus(ru(u8"Перенос отменён: выбранная фраза или сигнал уже изменились")); return false; }
    return loadAudioFile(file, context.track, std::make_shared<DropContext>(context), std::move(lease));
}

bool FLOWALIGNAudioProcessor::loadFile(const juce::File& file, Track track)
{
    return loadAudioFile(file, track, {}, {});
}

bool FLOWALIGNAudioProcessor::loadAudioFile(const juce::File& file, Track track,
    std::shared_ptr<const DropContext> context, std::shared_ptr<void> lease)
{
    if (track == Track::result) return false;
    const bool started = launch([this, file, track, context, lease]
    {
        stage.store(1); setStatus(ru(u8"Загрузка: ") + file.getFileName());
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader) throw std::runtime_error(u8"формат файла не поддерживается");
        if (reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate < 8000.0 || reader->sampleRate > 192000.0
            || reader->lengthInSamples < 1 || reader->lengthInSamples > static_cast<int64_t>(reader->sampleRate * 120.0))
            throw std::runtime_error(u8"нужен моно/стерео файл до 120 секунд, 8–192 кГц");
        juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        if (!reader->read(&audio, 0, audio.getNumSamples(), 0, true, true)) throw std::runtime_error(u8"ошибка чтения файла");
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                if (!std::isfinite(audio.getSample(c, i))) throw std::runtime_error(u8"файл содержит некорректные сэмплы");
        if (cancelled.load()) throw std::runtime_error("cancelled");
        auto clip = makeClip(std::move(audio), reader->sampleRate, file.getFileName());
        if (context && !isDropContextCurrent(*context))
            throw std::runtime_error(u8"перенос отменён: фраза или сигнал изменились");
        auto next = std::make_shared<Session>(*snapshot());
        if (context)
        {
            const auto other = track == Track::reference ? next->dub : next->reference;
            if (other && std::abs(other->duration() - clip->duration())
                > juce::jmax(1.0 / other->sampleRate, 1.0 / clip->sampleRate) + 1.0e-9)
                throw std::runtime_error((ru(u8"длины регионов различаются: ") + juce::String(other->duration(), 6)
                    + ru(u8" с и ") + juce::String(clip->duration(), 6)
                    + ru(u8" с. Обрежьте их одинаково; для другой пары нажмите «+ Фраза»")).toStdString());
        }
        if (track == Track::reference) { next->reference = std::move(clip); next->referenceStartSeconds = -1.0; }
        else { next->dub = std::move(clip); next->anchorSeconds = -1.0; }
        next->result.reset(); next->resultId.clear(); next->sourceSeconds.clear(); next->hasRenderedSettings = false;
        if (audioBytes(*next) > 256u * 1024u * 1024u) throw std::runtime_error(u8"превышен лимит памяти сессии 256 МБ");
        if (context) { stopPreview(); setValue("syncHost", 0.0f); }
        previewing.store(false); publish(std::move(next)); progress.store(1.0f);
        setStatus((context ? ru(u8"Регион загружен: ") : ru(u8"Файл загружен: ")) + file.getFileName());
    });
    if (started && !context)
    {
        stopPreview();
        setValue("syncHost", 0.0f);
    }
    return started;
}

bool FLOWALIGNAudioProcessor::startAlignment()
{
    const auto input = snapshot();
    if (!input->reference || !input->dub)
    { setStatus(ru(u8"Нужны оба сигнала: опорный вокал и дубль")); return false; }
    const auto options = settings();
    const bool started = launch([this, input, options]
    {
        stage.store(2); setStatus(ru(u8"Анализ тайминга и выравнивание вокала…"));
        auto ref = flowalign::resample(input->reference->audio, input->reference->sampleRate, input->dub->sampleRate);
        auto aligned = flowalign::align(ref, input->dub->audio, input->dub->sampleRate, options, progress, cancelled);
        if (cancelled.load()) throw std::runtime_error("cancelled");
        auto next = std::make_shared<Session>(*input);
        next->result = makeClip(std::move(aligned.audio), input->dub->sampleRate, ru(u8"Выравненный вокал"));
        next->resultId = juce::Uuid().toString();
        next->sourceSeconds = std::move(aligned.sourceSeconds); next->correctionMs = aligned.meanCorrectionMs;
        next->renderedSettings = options; next->hasRenderedSettings = true;
        if (audioBytes(*next) > 256u * 1024u * 1024u) throw std::runtime_error(u8"превышен лимит памяти сессии 256 МБ");
        publish(std::move(next));
        setStatus(ru(u8"Готово. Сравните «До / После» или перетащите результат в Logic"));
        selectedPreviewTrack.store(static_cast<int>(Track::result));
    });
    if (started) stopPreview();
    return started;
}

void FLOWALIGNAudioProcessor::startPreview()
{
    startPreview(value("after") > 0.5f && snapshot()->result ? Track::result : Track::dub);
}

bool FLOWALIGNAudioProcessor::startPreview(Track track)
{
    if (isCapturing() || capturePending.load() || busy.load()) return false;
    const auto session = snapshot();
    const auto* clip = clipFor(*session, track);
    if (!clip)
    { setStatus(ru(u8"Этот сигнал ещё не готов для прослушивания")); return false; }
    systemPreview.stop(); systemPreviewLoaded = false;
    if (previewTrack() != track || previewCursor.load() >= clip->duration()) seek(0.0);
    selectedPreviewTrack.store(static_cast<int>(track));
    previewing.store(true);
    setStatus(ru(u8"Прослушивание через аудиовыход плагина"));
    return true;
}

bool FLOWALIGNAudioProcessor::makePreviewWave(Track track, juce::MemoryBlock& bytes) const
{
    bytes.reset();
    const auto session = snapshot();
    const auto* clip = clipFor(*session, track);
    if (!clip) return false;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(clip->sampleRate)
        .withNumChannels(clip->audio.getNumChannels()).withBitsPerSample(24));
    return writer && writer->writeFromAudioSampleBuffer(clip->audio, 0, clip->audio.getNumSamples());
}

bool FLOWALIGNAudioProcessor::startSystemPreview(Track track, double position)
{
    if (isCapturing() || capturePending.load() || busy.load())
    { setStatus(ru(u8"Дождитесь завершения записи или выравнивания")); return false; }
    juce::MemoryBlock wave;
    if (!makePreviewWave(track, wave))
    {
        setStatus(track == Track::result ? ru(u8"Результат не готов. Сначала нажмите «Выровнять»")
            : track == Track::reference ? ru(u8"Сначала запишите эталон через Side Chain") : ru(u8"Сначала запишите дубль"));
        return false;
    }
    stopPreview();
    selectedPreviewTrack.store(static_cast<int>(track));
    juce::String error;
    if (!systemPreview.start(wave, position, value("loop") > 0.5f, error))
    { setStatus(error); return false; }
    systemPreviewLoaded = true; previewFinishedNotified = false;
    if (track != Track::reference) setValue("after", track == Track::result ? 1 : 0);
    setValue("syncHost", 0);
    setStatus((track == Track::reference ? ru(u8"Эталон") : track == Track::dub ? ru(u8"Дубль") : ru(u8"Результат"))
        + ru(u8" · прослушивание через системный выход macOS"));
    return true;
}

void FLOWALIGNAudioProcessor::pollPreview()
{
    if (!systemPreviewLoaded) return;
    systemPreview.setLooping(value("loop") > 0.5f);
    if (!systemPreview.isPlaying() && !previewFinishedNotified)
    { previewFinishedNotified = true; setStatus(ru(u8"Прослушивание завершено. Можно выбрать другой сигнал")); }
}

void FLOWALIGNAudioProcessor::seek(double seconds)
{
    if (systemPreviewLoaded) systemPreview.seek(seconds);
    previewCursor.store(seconds); playbackPosition.store(seconds); seekGeneration.fetch_add(1);
}

void FLOWALIGNAudioProcessor::stopPreview()
{
    systemPreview.stop(); systemPreviewLoaded = false;
    previewing.store(false); outputPeak.store(0.0f); seek(0.0);
}

bool FLOWALIGNAudioProcessor::anchorToHost()
{
    if (!hostPositionKnown.load()) { setStatus(ru(u8"Logic не передал позицию. Кратко запустите воспроизведение")); return false; }
    auto next = std::make_shared<Session>(*snapshot());
    next->anchorSeconds = hostSeconds.load(); publish(std::move(next));
    setStatus(ru(u8"Начало аудио привязано к текущей позиции Logic")); return true;
}

const FLOWALIGNAudioProcessor::Clip* FLOWALIGNAudioProcessor::clipFor(const Pair& session, Track track)
{
    return track == Track::reference ? session.reference.get() : track == Track::dub ? session.dub.get() : session.result.get();
}

bool FLOWALIGNAudioProcessor::exportWave(const juce::File& file, Track track) const
{
    const auto session = snapshot();
    const auto* clip = clipFor(*session, track);
    if (clip == nullptr) return false;
    auto output = file.createOutputStream();
    if (!output) return false;
    output->setPosition(0); output->truncate();
    std::unique_ptr<juce::OutputStream> stream(std::move(output));
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate(clip->sampleRate)
        .withNumChannels(clip->audio.getNumChannels()).withBitsPerSample(24);
    auto writer = format.createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(clip->audio, 0, clip->audio.getNumSamples());
}

bool FLOWALIGNAudioProcessor::exportTiming(const juce::File& file) const
{
    const auto session = snapshot();
    if (session->sourceSeconds.empty()) return false;
    juce::String csv = ru(u8"Результат_сек,Источник_сек\n");
    const double hop = std::floor(session->dub->sampleRate * 0.01) / session->dub->sampleRate;
    for (size_t i = 0; i < session->sourceSeconds.size(); ++i)
        csv += juce::String(static_cast<double>(i) * hop, 6) + "," + juce::String(session->sourceSeconds[i], 6) + "\n";
    return file.replaceWithText(csv, false, false, "\n");
}

void FLOWALIGNAudioProcessor::clearSession()
{
    if (busy.load() || isCapturing() || capturePending.load()) return;
    dropEpoch.fetch_add(1);
    stopPreview(); publish(std::make_shared<Session>());
    setStatus(ru(u8"Сессия очищена. Загрузите файлы или захватите дорожки"));
}

bool FLOWALIGNAudioProcessor::sameSettings(const flowalign::Settings& a, const flowalign::Settings& b)
{
    return std::abs(a.tightness - b.tightness) < 0.00001f && std::abs(a.naturalness - b.naturalness) < 0.00001f
        && std::abs(a.maxStretch - b.maxStretch) < 0.00001f && a.preservePitch == b.preservePitch
        && a.protectTransients == b.protectTransients && a.autoGain == b.autoGain;
}

bool FLOWALIGNAudioProcessor::hasCurrentResult() const
{
    const auto s = snapshot();
    return s->result && (!s->hasRenderedSettings || sameSettings(s->renderedSettings, settings()));
}

size_t FLOWALIGNAudioProcessor::audioBytes(const Session& s)
{
    size_t bytes = 0;
    auto add = [&](const Pair& p)
    {
        for (auto track : { Track::reference, Track::dub, Track::result })
            if (const auto* clip = clipFor(p, track))
                bytes += static_cast<size_t>(clip->audio.getNumChannels()) * static_cast<size_t>(clip->audio.getNumSamples()) * sizeof(float);
    };
    add(s); for (const auto& p : s.otherPairs) add(p);
    return bytes;
}

bool FLOWALIGNAudioProcessor::newPair()
{
    if (busy.load() || isCapturing() || capturePending.load()) return false;
    auto next = std::make_shared<Session>(*snapshot());
    if (next->otherPairs.size() >= 7)
    { setStatus(ru(u8"В одной сессии можно хранить до восьми пар фрагментов")); return false; }
    auto previous = static_cast<const Pair&>(*next);
    next->otherPairs.push_back(previous);
    static_cast<Pair&>(*next) = Pair();
    next->id = static_cast<int>(next->otherPairs.size());
    dropEpoch.fetch_add(1);
    stopPreview(); publish(std::move(next));
    setStatus(ru(u8"Новая пара. Запишите эталон и дубль с нужных позиций"));
    return true;
}

bool FLOWALIGNAudioProcessor::selectPair(int id)
{
    if (busy.load() || isCapturing() || capturePending.load()) return false;
    auto next = std::make_shared<Session>(*snapshot());
    if (next->id == id) return true;
    auto found = std::find_if(next->otherPairs.begin(), next->otherPairs.end(), [id](const Pair& p) { return p.id == id; });
    if (found == next->otherPairs.end()) return false;
    Pair selected = *found;
    *found = static_cast<const Pair&>(*next);
    static_cast<Pair&>(*next) = std::move(selected);
    const auto options = next->renderedSettings;
    const bool restore = next->hasRenderedSettings;
    dropEpoch.fetch_add(1);
    stopPreview(); publish(std::move(next));
    if (restore)
    {
        setValue("tightness", options.tightness); setValue("naturalness", options.naturalness); setValue("maxStretch", options.maxStretch);
        setValue("preservePitch", options.preservePitch ? 1 : 0); setValue("protectTransients", options.protectTransients ? 1 : 0);
        setValue("autoGain", options.autoGain ? 1 : 0);
    }
    setStatus(ru(u8"Выбрана пара ") + juce::String(id + 1));
    return true;
}

juce::StringArray FLOWALIGNAudioProcessor::pairNames() const
{
    const auto s = snapshot();
    juce::StringArray names;
    for (int id = 0; id <= static_cast<int>(s->otherPairs.size()); ++id)
    {
        const Pair* p = s.get();
        if (s->id != id)
            for (const auto& candidate : s->otherPairs) if (candidate.id == id) { p = &candidate; break; }
        names.add(ru(u8"Пара ") + juce::String(id + 1) + (p->result ? ru(u8" · готово") : p->dub && p->reference ? ru(u8" · записано") : ru(u8" · запись")));
    }
    return names;
}

juce::File FLOWALIGNAudioProcessor::prepareResultForLogic(const juce::File& storage)
{
    if (busy.load() || isCapturing() || capturePending.load())
    { setStatus(ru(u8"Дождитесь завершения записи или выравнивания")); return {}; }
    if (!snapshot()->result)
    { setStatus(ru(u8"Результат не готов. Сначала нажмите «Выровнять»")); return {}; }
    const auto folder = storage != juce::File() ? storage : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("FLOWMINNERS/FLOWALIGN/Results");
    if (folder.createDirectory().failed())
    { setStatus(ru(u8"Не удалось подготовить аудио для Logic")); return {}; }
    const auto s = snapshot();
    const auto file = folder.getChildFile("FLOWALIGN-" + (s->resultId.isEmpty() ? juce::Uuid().toString() : s->resultId) + ".wav");
    if (file.existsAsFile() && file.withFileExtension("json").existsAsFile())
    {
        const auto previous = juce::JSON::parse(file.withFileExtension("json"));
        if (previous["resultId"].toString() == s->resultId
            && static_cast<double>(previous["projectStartSeconds"]) == s->anchorSeconds) return file;
    }
    juce::TemporaryFile temporary(file);
    if (!exportWave(temporary.getFile(), Track::result) || !temporary.overwriteTargetFileWithTemporary())
    { setStatus(ru(u8"Не удалось подготовить WAV для Logic. Проверьте доступ к папке результатов")); return {}; }
    auto manifest = std::make_unique<juce::DynamicObject>();
    manifest->setProperty("schema", 1); manifest->setProperty("pair", s->id + 1);
    manifest->setProperty("resultId", s->resultId);
    manifest->setProperty("projectStartSeconds", s->anchorSeconds);
    manifest->setProperty("referenceStartSeconds", s->referenceStartSeconds);
    manifest->setProperty("sampleRate", s->result->sampleRate);
    manifest->setProperty("frames", s->result->audio.getNumSamples());
    manifest->setProperty("automaticPlacement", false);
    if (!file.withFileExtension("json").replaceWithText(juce::JSON::toString(juce::var(manifest.release()))))
    { file.deleteFile(); setStatus(ru(u8"Не удалось сохранить позицию результата для Logic")); return {}; }
    setStatus(ru(u8"Перетащите результат в Logic. Начало дубля: ") + (s->anchorSeconds >= 0.0 ? juce::String(s->anchorSeconds, 3) + ru(u8" с") : ru(u8"не задано")));
    return file;
}

void FLOWALIGNAudioProcessor::getStateInformation(juce::MemoryBlock& data)
{
    juce::MemoryOutputStream destination(data, false);
    destination.writeInt(0x46414c33);
    juce::GZIPCompressorOutputStream compressed(&destination, 1, false);
    parameters.copyState().writeToStream(compressed);
    const auto session = snapshot();
    compressed.writeInt(static_cast<int>(session->otherPairs.size()) + 1);
    compressed.writeInt(session->id);
    auto writePair = [&](const Pair& pair)
    {
        compressed.writeInt(pair.id);
        compressed.writeString(pair.resultId);
        compressed.writeDouble(pair.anchorSeconds); compressed.writeDouble(pair.referenceStartSeconds); compressed.writeDouble(pair.correctionMs);
        compressed.writeBool(pair.hasRenderedSettings);
        const auto& o = pair.renderedSettings;
        compressed.writeFloat(o.tightness); compressed.writeFloat(o.naturalness); compressed.writeFloat(o.maxStretch);
        compressed.writeBool(o.preservePitch); compressed.writeBool(o.protectTransients); compressed.writeBool(o.autoGain);
        for (auto track : { Track::reference, Track::dub, Track::result })
        {
            const auto* clip = clipFor(pair, track);
            compressed.writeBool(clip != nullptr);
            if (!clip) continue;
            compressed.writeString(clip->name); compressed.writeDouble(clip->sampleRate);
            compressed.writeInt(clip->audio.getNumChannels()); compressed.writeInt(clip->audio.getNumSamples());
            for (int c = 0; c < clip->audio.getNumChannels(); ++c)
                compressed.write(clip->audio.getReadPointer(c), static_cast<size_t>(clip->audio.getNumSamples()) * sizeof(float));
        }
        compressed.writeInt(static_cast<int>(pair.sourceSeconds.size()));
        for (auto time : pair.sourceSeconds) compressed.writeDouble(time);
    };
    writePair(*session);
    for (const auto& p : session->otherPairs) writePair(p);
    compressed.writeInt(0x454e4433);
}

void FLOWALIGNAudioProcessor::setStateInformation(const void* data, int size)
{
    if (size < 4) return;
    juce::MemoryInputStream source(data, static_cast<size_t>(size), false);
    const auto magic = source.readInt();
    const bool legacy = magic == 0x46414c32;
    if (!legacy && magic != 0x46414c33) return;
    juce::GZIPDecompressorInputStream stream(&source, false);
    auto tree = juce::ValueTree::readFromStream(stream);
    if (!tree.isValid() || tree.getType() != parameters.state.getType()) return;
    auto next = std::make_shared<Session>();
    const int pairCount = legacy ? 1 : stream.readInt();
    const int activeId = legacy ? 0 : stream.readInt();
    if (pairCount < 1 || pairCount > 8 || activeId < 0 || activeId >= pairCount) return;
    std::array<bool, 8> seen {};
    size_t totalBytes = 0;
    for (int n = 0; n < pairCount; ++n)
    {
        Pair pair;
        pair.id = legacy ? 0 : stream.readInt();
        pair.resultId = legacy ? juce::String() : stream.readString();
        if (pair.resultId.isNotEmpty() && (pair.resultId.length() > 64 || !pair.resultId.containsOnly("0123456789abcdefABCDEF-"))) return;
        if (pair.id < 0 || pair.id >= pairCount || seen[static_cast<size_t>(pair.id)]) return;
        seen[static_cast<size_t>(pair.id)] = true;
        pair.anchorSeconds = stream.readDouble();
        pair.referenceStartSeconds = legacy ? -1.0 : stream.readDouble();
        pair.correctionMs = stream.readDouble();
        if (!std::isfinite(pair.anchorSeconds) || !std::isfinite(pair.referenceStartSeconds) || !std::isfinite(pair.correctionMs)) return;
        if (!legacy)
        {
            pair.hasRenderedSettings = stream.readBool();
            auto& o = pair.renderedSettings;
            o.tightness = stream.readFloat(); o.naturalness = stream.readFloat(); o.maxStretch = stream.readFloat();
            o.preservePitch = stream.readBool(); o.protectTransients = stream.readBool(); o.autoGain = stream.readBool();
            if (!std::isfinite(o.tightness) || o.tightness < 0 || o.tightness > 1 || !std::isfinite(o.naturalness)
                || o.naturalness < 0 || o.naturalness > 1 || !std::isfinite(o.maxStretch) || o.maxStretch < 0.05f || o.maxStretch > 0.8f) return;
        }
        for (auto track : { Track::reference, Track::dub, Track::result })
        {
            if (!stream.readBool()) continue;
            const auto name = stream.readString(); const double rate = stream.readDouble();
            const int channels = stream.readInt(), length = stream.readInt();
            if (!std::isfinite(rate) || rate < 8000.0 || rate > 192000.0 || channels < 1 || channels > 2
                || length < 1 || length > static_cast<int>(rate * 120.0)) return;
            totalBytes += static_cast<size_t>(channels) * static_cast<size_t>(length) * sizeof(float);
            if (totalBytes > 256u * 1024u * 1024u) return;
            juce::AudioBuffer<float> audio(channels, length);
            for (int c = 0; c < channels; ++c)
            {
                const int bytes = length * static_cast<int>(sizeof(float));
                if (stream.read(audio.getWritePointer(c), bytes) != bytes) return;
                for (int i = 0; i < length; ++i) if (!std::isfinite(audio.getSample(c, i))) return;
            }
            auto clip = makeClip(std::move(audio), rate, name);
            if (track == Track::reference) pair.reference = std::move(clip);
            else if (track == Track::dub) pair.dub = std::move(clip);
            else pair.result = std::move(clip);
        }
        const int count = stream.readInt();
        if (count < 0 || count > 12002) return;
        for (int i = 0; i < count; ++i)
        {
            const double time = stream.readDouble();
            if (!std::isfinite(time) || time < 0.0 || time > 121.0) return;
            pair.sourceSeconds.push_back(time);
        }
        if (pair.id == activeId) static_cast<Pair&>(*next) = std::move(pair);
        else next->otherPairs.push_back(std::move(pair));
    }
    if (stream.readInt() != (legacy ? 0x454e4432 : 0x454e4433)) return;
    cancelled.store(true);
    const std::lock_guard<std::mutex> lock(jobMutex);
    if (worker.joinable()) worker.join();
    stopCapture(); pollCapture(); stopPreview();
    dropEpoch.fetch_add(1);
    parameters.replaceState(tree);
    if (legacy && next->result)
    {
        next->renderedSettings = settings(); next->hasRenderedSettings = true;
        next->resultId = juce::Uuid().toString();
    }
    publish(std::move(next));
    setStatus(ru(u8"Фрагменты и результаты восстановлены из проекта"));
}

juce::AudioProcessorEditor* FLOWALIGNAudioProcessor::createEditor() { return new FLOWALIGNAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FLOWALIGNAudioProcessor(); }
