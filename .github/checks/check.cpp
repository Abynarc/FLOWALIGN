#include "../../Source/PluginEditor.h"
#include <cmath>
#include <cstring>
#include <chrono>
#include <iostream>
#include <stdexcept>

using Processor = FLOWALIGNAudioProcessor;
using Track = Processor::Track;
static void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

static void waitFor(Processor& processor)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 30000;
    while (processor.isBusy())
    {
        require(juce::Time::getMillisecondCounterHiRes() < deadline, "Worker timed out");
        juce::Thread::sleep(10);
    }
}

static juce::AudioBuffer<float> vocal(double rate, bool dub)
{
    const std::array<double, 4> events = dub ? std::array<double, 4> { 0.64, 1.35, 2.02, 2.99 }
                                           : std::array<double, 4> { 0.50, 1.25, 2.10, 2.85 };
    juce::AudioBuffer<float> audio(dub ? 2 : 1, static_cast<int>(rate * 3.5));
    audio.clear();
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const double t = i / rate;
        double env = 0.0;
        for (size_t n = 0; n < events.size(); ++n)
        {
            const double distance = (t - events[n]) / (0.045 + n * 0.012);
            env += (0.7 + 0.1 * n) * std::exp(-0.5 * distance * distance);
        }
        const double phase = juce::MathConstants<double>::twoPi * (dub ? 223.0 : 317.0) * t;
        const float value = static_cast<float>(0.45 * env * (std::sin(phase) + 0.2 * std::sin(3 * phase)));
        audio.setSample(0, i, value);
        if (dub) audio.setSample(1, i, -0.7f * value);
    }
    return audio;
}

static double energyPeak(const juce::AudioBuffer<float>& audio, double rate, double centre)
{
    double maximum = -1.0;
    int best = 0;
    for (int i = static_cast<int>((centre - 0.25) * rate); i < static_cast<int>((centre + 0.25) * rate); i += 32)
    {
        double sum = 0.0;
        for (int j = -80; j < 80; ++j)
            if (juce::isPositiveAndBelow(i + j, audio.getNumSamples())) sum += std::pow(audio.getSample(0, i + j), 2.0);
        if (sum > maximum) { maximum = sum; best = i; }
    }
    return best / rate;
}

static void writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    auto fileStream = file.createOutputStream();
    require(fileStream != nullptr, "Cannot open fixture output");
    fileStream->setPosition(0); fileStream->truncate();
    std::unique_ptr<juce::OutputStream> stream(std::move(fileStream));
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(rate)
        .withNumChannels(audio.getNumChannels()).withBitsPerSample(24));
    require(writer && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Cannot write fixture WAV");
}

static void snapshot(Processor& processor, const juce::File& folder, const char* filename, int width = 1280)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(width, juce::roundToInt(width * 980.0 / 1600.0));
    juce::Thread::sleep(60); juce::Timer::callPendingTimersSynchronously();
    auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    auto stream = folder.getChildFile(filename).createOutputStream();
    require(stream != nullptr, "Cannot open snapshot output");
    stream->setPosition(0); stream->truncate();
    require(stream && juce::PNGImageFormat().writeImageToStream(image, *stream), "Cannot save screenshot");
}

static void defaultSettingsChecks()
{
    const flowalign::Settings defaults;
    require(defaults.tightness == 1.0f && defaults.naturalness == 1.0f, "Engine defaults must be 100%");
    Processor p;
    for (const auto* id : { "tightness", "naturalness" })
    {
        const auto* parameter = p.parameters.getParameter(id);
        require(parameter && parameter->convertFrom0to1(parameter->getDefaultValue()) == 1.0f,
                "Host parameter default must be 100%");
        require(p.value(id) == 1.0f, "New instance must start at 100%");
    }
    p.setValue("tightness", 0.37f); p.setValue("naturalness", 0.42f);
    juce::MemoryBlock state; p.getStateInformation(state);
    Processor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(restored.createEditor());
    require(std::abs(restored.value("tightness") - 0.37f) < 0.00001f
            && std::abs(restored.value("naturalness") - 0.42f) < 0.00001f,
            "New defaults overwrote saved project settings");
    bool presetFound = false;
    for (auto* child : editor->getChildren())
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            if (combo->getNumItems() == 3 && combo->getItemText(0) == juce::String::fromUTF8(u8"По умолчанию"))
            {
                combo->setSelectedId(2, juce::sendNotificationSync);
                combo->setSelectedId(1, juce::sendNotificationSync);
                presetFound = true;
            }
    require(presetFound && restored.value("tightness") == 1.0f && restored.value("naturalness") == 1.0f,
            "Default preset must restore 100% / 100%");
    std::cout << "PASS: 100% engine/host/instance/default preset; saved settings survive opening editor\n";
}

static void audioChecks()
{
    int cases = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    for (int channels : { 1, 2 })
    for (int sideChannels : { 0, 1, 2 })
    {
        Processor p;
        auto layout = p.getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.inputBuses.set(1, juce::AudioChannelSet::canonicalChannelSet(sideChannels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        require(p.setBusesLayout(layout), "Audio layout rejected");
        p.setRateAndBufferSizeDetails(rate, 128); p.prepareToPlay(rate, 128);
        for (bool capturing : { false, true })
        {
            if (capturing) { require(p.startCapture(), "Start capture failed"); require(!p.startCapture(), "Active capture was restarted"); }
            for (int block = 0; block < 3; ++block)
            {
                juce::AudioBuffer<float> buffer(channels + sideChannels, 128);
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    for (int i = 0; i < 128; ++i) buffer.setSample(c, i, ((i % 17) - 8) * 0.025f * (c + 1));
                juce::AudioBuffer<float> before; before.makeCopyOf(buffer);
                juce::MidiBuffer midi; p.processBlock(buffer, midi);
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    require(std::memcmp(buffer.getReadPointer(c), before.getReadPointer(c), 128 * sizeof(float)) == 0, "Passthrough changed samples");
            }
            if (capturing)
            {
                p.stopCapture(); p.pollCapture();
                auto session = p.snapshot();
                require(session->dub && session->dub->audio.getNumSamples() == 384, "Capture missing samples");
                require((session->reference != nullptr) == (sideChannels > 0), "Sidechain capture mismatch");
            }
        }
        require(p.getPeak(false) > 0.0f, "Live meters did not update");
        ++cases;
    }
    std::cout << "PASS: " << cases << " rates/layouts, bit-exact passthrough, capture, sidechain, live meters\n";
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    bool playing = false;
    int64_t samples = 480000;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setIsPlaying(playing); info.setTimeInSamples(samples); return info;
    }
};

static void transportChecks()
{
    Processor p; p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    TestPlayHead host; p.setPlayHead(&host);
    juce::AudioBuffer<float> buffer(2, 128); buffer.clear(); buffer.applyGain(0.5f);
    juce::MidiBuffer midi;
    require(p.startCapture(), "Capture arm failed");
    p.processBlock(buffer, midi); require(p.capturedSamples() == 0, "Captured while Logic stopped");
    host.playing = true; p.processBlock(buffer, midi); host.samples += 128;
    p.processBlock(buffer, midi); host.samples += 128;
    host.playing = false; p.processBlock(buffer, midi); p.pollCapture();
    require(!p.isCapturing() && p.snapshot()->dub && p.snapshot()->dub->audio.getNumSamples() == 256, "Host stop did not finish capture");
    require(std::abs(p.snapshot()->anchorSeconds - 10.0) < 0.000001, "Wrong transport anchor");
    p.setPlayHead(nullptr);
    std::cout << "PASS: armed capture waits for Logic, stops on transport stop, records timeline anchor\n";
}

static void captureWorkflowChecks(const juce::File& folder)
{
    Processor p;
    auto layout = p.getBusesLayout(); layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    require(p.setBusesLayout(layout), "Capture workflow layout rejected");
    p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    TestPlayHead host; p.setPlayHead(&host);
    auto ref = vocal(48000, false), dub = vocal(48000, true);
    juce::AudioBuffer<float> buffer(4, 128); juce::MidiBuffer midi;
    auto record = [&](Processor::CaptureMode mode, double start, bool referenceSignal = true, bool mainSignal = true)
    {
        host.samples = static_cast<int64_t>(start * 48000); host.playing = false;
        require(p.startCapture(mode), "Independent capture arm failed");
        require(!p.newPair() && !p.selectPair(0), "Pair changed during capture");
        require(p.isCapturingTrack(Track::reference) == (mode != Processor::CaptureMode::dub), "Wrong live reference state");
        buffer.clear(); p.processBlock(buffer, midi);
        require(p.capturedSamples() == 0, "Capture started before Play");
        host.playing = true;
        for (int position = 0; position < ref.getNumSamples(); position += 128)
        {
            buffer.clear();
            for (int i = 0; i < 128 && position + i < ref.getNumSamples(); ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    buffer.setSample(c, i, mainSignal ? dub.getSample(c, position + i) : 0.0f);
                    buffer.setSample(c + 2, i, referenceSignal ? ref.getSample(0, position + i) : 0.0f);
                }
            }
            juce::AudioBuffer<float> dry; dry.makeCopyOf(buffer);
            p.processBlock(buffer, midi);
            for (int c = 0; c < 2; ++c)
                require(std::memcmp(buffer.getReadPointer(c), dry.getReadPointer(c), 128 * sizeof(float)) == 0,
                        "Capture output was contaminated by old processed result");
            host.samples += 128;
        }
        host.playing = false; p.processBlock(buffer, midi); p.pollCapture();
        require(!p.isCapturing() && !p.isCapturePending(), "Capture did not finalize");
    };
    record(Processor::CaptureMode::reference, 10, true, false);
    require(p.snapshot()->reference && !p.snapshot()->dub, "Reference-only capture also recorded a dub");
    require(p.snapshot()->referenceStartSeconds == 10, "Wrong separate reference anchor");
    const auto firstReference = p.snapshot()->reference;
    record(Processor::CaptureMode::dub, 50);
    require(p.snapshot()->reference == firstReference, "Dub recording replaced the reference");
    require(p.snapshot()->anchorSeconds == 50 && p.snapshot()->referenceStartSeconds == 10, "Independent timeline positions were lost");
    p.setValue("tightness", 0.7f); p.setValue("naturalness", 0.6f); // Explicit legacy render settings.
    require(p.startAlignment(), "Independent capture alignment refused"); waitFor(p);
    require(p.hasCurrentResult() && p.snapshot()->anchorSeconds == 50, "Result moved to reference position");
    const auto result0 = p.snapshot()->result;
    host.samples = 50 * 48000 + 24000; host.playing = true;
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(buffer.getWritePointer(c), 0.125f, 128);
    p.processBlock(buffer, midi);
    require(std::abs(buffer.getSample(0, 32) - result0->audio.getSample(0, 24032)) < 0.00001f,
            "Captured result did not play at the dub's original timeline position");
    host.playing = false;
    const auto exportFolder = folder.getChildFile("managed-results");
    const auto prepared = p.prepareResultForLogic(exportFolder);
    require(prepared.existsAsFile() && prepared.getParentDirectory() == exportFolder, "Automatic result preparation failed");
    const auto manifest = juce::JSON::parse(prepared.withFileExtension("json"));
    require(static_cast<double>(manifest["projectStartSeconds"]) == 50.0 && static_cast<double>(manifest["referenceStartSeconds"]) == 10.0,
            "Prepared audio lost exact placement metadata");
    require(!static_cast<bool>(manifest["automaticPlacement"]), "Unverified Logic placement falsely advertised");
    const auto repeated = p.prepareResultForLogic(exportFolder);
    require(repeated == prepared, "Repeated preparation generated duplicate audio");
    p.setValue("tightness", 0.5f);
    require(!p.hasCurrentResult() && p.prepareResultForLogic(exportFolder) == prepared && p.snapshot()->result == result0,
            "Changing a knob or preparing transfer discarded the last generated result");
    p.setValue("tightness", 0.7f);
    require(p.hasCurrentResult(), "Original render settings not recognized");
    require(p.newPair() && p.snapshot()->id == 1 && !p.snapshot()->reference && !p.snapshot()->dub, "New phrase did not start empty");
    record(Processor::CaptureMode::both, 90);
    p.setValue("tightness", 0.5f);
    require(p.startAlignment(), "Second phrase alignment refused"); waitFor(p);
    require(p.hasCurrentResult() && p.snapshot()->anchorSeconds == 90, "Second phrase result missing");
    juce::MemoryBlock state; p.getStateInformation(state);
    Processor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.snapshot()->id == 1 && restored.snapshot()->otherPairs.size() == 1 && restored.hasCurrentResult(), "Multi-phrase session not restored");
    require(restored.selectPair(0) && restored.snapshot()->anchorSeconds == 50 && restored.snapshot()->referenceStartSeconds == 10,
            "Restored phrase lost separate capture positions");
    require(restored.hasCurrentResult() && std::abs(restored.value("tightness") - 0.7f) < 0.00001f, "Per-phrase render settings not restored");
    require(std::memcmp(restored.snapshot()->result->audio.getReadPointer(0), result0->audio.getReadPointer(0),
            result0->audio.getNumSamples() * sizeof(float)) == 0, "Archived phrase audio changed during restore");
    const auto revision = restored.snapshot()->revision;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize() / 2));
    require(restored.snapshot()->revision == revision, "Truncated multi-phrase state replaced valid audio");
    require(p.selectPair(0) && p.snapshot()->result == result0, "Selecting phrase lost rendered audio");
    record(Processor::CaptureMode::dub, 50);
    require(p.snapshot()->reference == firstReference && !p.snapshot()->result, "Dub retake did not invalidate result or lost reference");
    const auto keptDub = p.snapshot()->dub;
    record(Processor::CaptureMode::reference, 12, false);
    require(!p.snapshot()->reference && p.snapshot()->dub == keptDub && !p.startAlignment(), "Silent retake silently reused an old reference");
    for (int id = 2; id < 8; ++id) require(p.newPair(), "Phrase capacity was reduced unexpectedly");
    require(!p.newPair() && p.pairNames().size() == 8, "Eight-phrase memory guard failed");
    require(p.startCapture(), "Jump test did not arm");
    host.playing = true; host.samples = 100 * 48000; p.processBlock(buffer, midi);
    host.samples = 200 * 48000; p.processBlock(buffer, midi); p.pollCapture();
    require(!p.isCapturing() && p.snapshot()->dub->audio.getNumSamples() == 128 && p.snapshot()->anchorSeconds == 100,
            "Timeline jump joined unrelated capture positions");
    p.setPlayHead(nullptr);
    snapshot(restored, folder, "editor-capture-workflow.png");
    snapshot(restored, folder, "editor-capture-workflow-small.png", 1000);
    std::cout << "PASS: independent reference/dub capture at 10s/50s; silent-main sidechain; dry recording with prior result; separate anchors; 8 phrase slots; per-phrase settings; generated result retained after knob edit/transfer; automatic WAV; lossless multi-phrase restore\n";
}

static juce::Component* componentById(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* found = componentById(*child, id)) return found;
    return nullptr;
}

static void playbackTransferChecks(const juce::File& folder)
{
    writeWav(folder.getChildFile("player-reference.wav"), vocal(48000, false), 48000);
    writeWav(folder.getChildFile("player-dub.wav"), vocal(48000, true), 48000);
    Processor p;
    require(!p.startPreview(Track::reference) && !p.isPreviewing(), "Missing reference falsely started playback");
    require(p.loadFile(folder.getChildFile("player-reference.wav"), Track::reference), "Player reference load failed"); waitFor(p);
    require(p.startPreview(Track::reference), "Reference audition wrongly requires a dub"); p.stopPreview();
    require(p.loadFile(folder.getChildFile("player-dub.wav"), Track::dub), "Player dub load failed"); waitFor(p);
    require(p.startAlignment(), "Player alignment failed"); waitFor(p);
    require(p.snapshot()->result != nullptr, "Player result missing");
    p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> buffer(2, 128); juce::MidiBuffer midi;
    for (auto track : { Track::reference, Track::dub, Track::result })
    {
        const auto state = p.snapshot();
        const auto clip = track == Track::reference ? state->reference : track == Track::dub ? state->dub : state->result;
        require(p.startPreview(track), "Selected signal did not start in host preview");
        p.seek(0.5); buffer.clear(); p.processBlock(buffer, midi);
        require(p.previewTrack() == track && p.isPreviewing(), "Preview switched to a different signal");
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 128; ++i)
                require(std::abs(buffer.getSample(c, i) - clip->audio.getSample(juce::jmin(c, clip->audio.getNumChannels() - 1), 24000 + i)) < 0.00001f,
                        "Preview played wrong source or mixed incoming audio");
        p.stopPreview();
        juce::MemoryBlock wave;
        require(p.makePreviewWave(track, wave), "System audition WAV missing");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(wave, false), true));
        require(reader && reader->sampleRate == clip->sampleRate && reader->numChannels == static_cast<unsigned>(clip->audio.getNumChannels())
                && reader->lengthInSamples == clip->audio.getNumSamples(), "System audition changed source format/duration");
        juce::AudioBuffer<float> decoded(clip->audio.getNumChannels(), clip->audio.getNumSamples());
        require(reader->read(&decoded, 0, decoded.getNumSamples(), 0, true, true), "Cannot decode system audition WAV");
        for (int c = 0; c < decoded.getNumChannels(); ++c)
            for (int i = 0; i < decoded.getNumSamples(); ++i)
                require(std::abs(decoded.getSample(c, i) - clip->audio.getSample(c, i)) < 0.0000002f,
                        "System audition WAV contains a different recording");
    }
    const auto result = p.snapshot()->result;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    editor->createComponentSnapshot(editor->getLocalBounds());
    for (const auto* id : { "previewReference", "previewDub", "previewResult", "afterResult", "transferResult" })
        require(componentById(*editor, id) && componentById(*editor, id)->isEnabled(), "Recorded signal has no enabled play/transfer control");
    auto* transfer = dynamic_cast<juce::Button*>(componentById(*editor, "transferResult"));
    require(transfer && transfer->onClick, "Transfer hint action missing");
    transfer->onClick();
    require(p.snapshot()->result == result && componentById(*editor, "afterResult")->isEnabled(), "Clicking transfer disabled After or removed result");
    p.setValue("tightness", 0.25f);
    require(!p.hasCurrentResult() && p.startPreview(Track::result), "Knob edit disabled generated result playback"); p.stopPreview();
    require(p.prepareResultForLogic(folder.getChildFile("transfer-ready")).existsAsFile() && p.snapshot()->result == result,
            "Transfer preparation discarded result after a knob edit");
    juce::Timer::callPendingTimersSynchronously();
    require(componentById(*editor, "afterResult")->isEnabled() && componentById(*editor, "transferResult")->isEnabled(),
            "A generated result became inaccessible after parameters changed");
    snapshot(p, folder, "editor-three-players.png"); snapshot(p, folder, "editor-three-players-small.png", 1000);
    editor.reset();
    std::cout << "PASS: reference-only audition; three separately selected sources in render and in-memory WAV; source rates/channels preserved; 3 play controls; transfer click and knob edits retain playable/exportable result and enabled After\n";

    // Exercise the real macOS backend. This environment may have no output device.
    Processor offline;
    require(offline.loadFile(folder.getChildFile("player-reference.wav"), Track::reference), "System reference import failed"); waitFor(offline);
    const bool started = offline.startSystemPreview(Track::reference, 0.5);
    require(offline.getSampleRate() == 0 && offline.capturedSamples() == 0, "System player initialized host/input recording");
    if (started)
    {
        offline.releaseResources();
        require(offline.isPreviewing(), "Host suspension stopped independent system audition");
        const auto initial = offline.playbackSeconds();
        juce::Thread::sleep(150);
        require(offline.isPreviewing() && offline.playbackSeconds() > initial + 0.02, "Native player claims playback without advancing");
        offline.seek(0.8);
        require(std::abs(offline.playbackSeconds() - 0.8) < 0.1, "Native seek failed");
        offline.stopPreview(); require(!offline.isPreviewing(), "Native stop failed");
        std::cout << "PASS: native AVAudioPlayer advanced and stopped without host audio callbacks; physical audibility still requires Logic/device check\n";
    }
    else
    {
        require(!offline.isPreviewing() && offline.statusText().isNotEmpty(), "Unavailable system output falsely shows Playing");
        std::cout << "LIMITATION: native output unavailable here; failure is explicit and Playing remains false: " << offline.statusText() << '\n';
    }
}

static void referenceRoutingChecks()
{
    Processor p; auto layout = p.getBusesLayout(); layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    require(p.setBusesLayout(layout), "Routing test layout rejected"); p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> buffer(4, 128); juce::MidiBuffer midi;
    auto capture = [&](bool duplicate, bool oneSharedBlock = false)
    {
        require(p.startCapture(Processor::CaptureMode::reference), "Routing capture refused");
        for (int block = 0; block < 4; ++block)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float dub = static_cast<float>((i % 29) - 14) * 0.01f;
                const float ref = duplicate || (oneSharedBlock && block == 0) ? dub : static_cast<float>((i % 17) - 8) * 0.02f;
                for (int c = 0; c < 2; ++c) { buffer.setSample(c, i, dub); buffer.setSample(c + 2, i, ref); }
            }
            p.processBlock(buffer, midi);
        }
        p.stopCapture(); p.pollCapture();
    };
    capture(false);
    require(p.snapshot()->reference != nullptr && !p.referenceMirrorsDub(), "Independent sidechain rejected");
    capture(true);
    require(!p.snapshot()->reference && p.referenceMirrorsDub() && !p.startAlignment(), "Exact dub fallback was accepted as reference or old reference reused");
    capture(false, true);
    require(p.snapshot()->reference != nullptr && !p.referenceMirrorsDub(), "Common intro caused a false duplicate rejection");
    require(p.startCapture(Processor::CaptureMode::reference), "Moving stereo reference capture refused");
    for (int block = 0; block < 4; ++block)
    {
        for (int n = 0; n < 128; ++n)
        {
            buffer.setSample(0,n,0.0125f);buffer.setSample(1,n,0.0125f);
            const float ref=static_cast<float>((n%17)-8)*0.02f;
            buffer.setSample(2,n,block<2?ref:0.0f);
            buffer.setSample(3,n,block>=2?ref:0.0f);
        }
        p.processBlock(buffer,midi);
    }
    p.stopCapture();p.pollCapture();
    const auto moved=p.snapshot()->reference;
    require(moved&&moved->audio.getMagnitude(0,128)>0.07f&&moved->audio.getMagnitude(384,128)>0.07f,
            "Moving reference between stereo channels lost audio");
    require(std::abs(moved->audio.getSample(0,3)-(-0.05f))<0.000001f,
            "Normal stereo sum changed despite no phase cancellation");
    Processor missing;
    require(!missing.startCapture(Processor::CaptureMode::reference) && !missing.isCapturing(), "Unconnected reference falsely armed");
    std::cout << "PASS: independent sidechain accepted; exact main-input copy rejected; shared intro accepted; absent sidechain has explicit error\n";
}

static void captureWithoutEditorChecks()
{
    Processor p;
    auto layout=p.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());
    require(p.setBusesLayout(layout),"Closed editor capture layout");
    p.setRateAndBufferSizeDetails(48000,128);p.prepareToPlay(48000,128);
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());editor.reset();
    struct Listener final:juce::AudioProcessorListener
    {
        int changes=0;
        void audioProcessorParameterChanged(juce::AudioProcessor*,int,float)override{}
        void audioProcessorChanged(juce::AudioProcessor* processor,const ChangeDetails& details)override
        {
            if(details.nonParameterStateChanged)
            {
                juce::MemoryBlock state;processor->getStateInformation(state);
                ++changes;
            }
        }
    } listener;
    p.addListener(&listener);
    auto record=[&](int blocks)
    {
        require(p.startCapture(Processor::CaptureMode::both),"Closed editor capture refused");
        juce::AudioBuffer<float> buffer(4,128);juce::MidiBuffer midi;
        for(int block=0;block<blocks;++block)
        {
            for(int n=0;n<128;++n)
            {
                const float dub=0.1f*std::sin(static_cast<float>(block*128+n)*0.071f);
                const float ref=0.2f*std::sin(static_cast<float>(block*128+n)*0.117f);
                buffer.setSample(0,n,dub);buffer.setSample(1,n,dub);
                buffer.setSample(2,n,ref);buffer.setSample(3,n,-ref);
            }
            p.processBlock(buffer,midi);
        }
        p.stopCapture();
    };
    record(8);
    for(int retry=0;retry<10&&p.isCapturePending();++retry)
    {juce::Thread::sleep(40);juce::Timer::callPendingTimersSynchronously();}
    require(!p.isCapturePending()&&p.snapshot()->dub,"Capture depends on an open editor");
    require(p.snapshot()->reference&&p.snapshot()->reference->audio.getMagnitude(0,1024)>0.15f,
            "Anti-phase stereo sidechain disappeared");
    require(listener.changes>0,"Host did not receive changed audio state notification");
    record(4);
    juce::MemoryBlock state;p.getStateInformation(state);
    Processor restored;restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    require(restored.snapshot()->dub&&restored.snapshot()->dub->audio.getNumSamples()==512,
            "Immediate project save lost completed capture");
    p.removeListener(&listener);
    std::cout<<"PASS: capture with editor closed, anti-phase sidechain, reentrant host state notification, immediate save\n";
}

static void legacyStateChecks(const juce::File& folder)
{
    // Fixed 0.2.1 stream schema, independent of the current writer.
    juce::MemoryBlock legacy;
    Processor source;
    {
        juce::MemoryOutputStream bytes(legacy, false); bytes.writeInt(0x46414c32);
        juce::GZIPCompressorOutputStream out(&bytes, 1, false);
        source.parameters.copyState().writeToStream(out);
        out.writeDouble(42.5); out.writeDouble(7.0);
        for (int track = 0; track < 3; ++track)
        {
            const auto audio = vocal(16000, track != 0);
            out.writeBool(true); out.writeString("0.2.1 fixture"); out.writeDouble(16000);
            out.writeInt(audio.getNumChannels()); out.writeInt(audio.getNumSamples());
            for (int c = 0; c < audio.getNumChannels(); ++c)
                out.write(audio.getReadPointer(c), static_cast<size_t>(audio.getNumSamples()) * sizeof(float));
        }
        out.writeInt(0); out.writeInt(0x454e4432);
    }
    require(folder.getChildFile("legacy-0.2.1.state").replaceWithData(legacy.getData(), legacy.getSize()), "Legacy fixture write failed");
    Processor restored; restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    require(restored.snapshot()->reference && restored.snapshot()->dub && restored.hasCurrentResult()
            && restored.snapshot()->anchorSeconds == 42.5 && restored.snapshot()->otherPairs.empty(), "0.2.1 project migration failed");
    juce::MemoryBlock updated; restored.getStateInformation(updated);
    Processor reopened; reopened.setStateInformation(updated.getData(), static_cast<int>(updated.getSize()));
    require(reopened.hasCurrentResult() && reopened.snapshot()->anchorSeconds == 42.5, "Migrated session did not survive a second save");
    std::cout << "PASS: fixed 0.2.1 session migrates to multi-phrase format without losing audio or anchor\n";
}

static void fileOnlyChecks(const juce::File& folder)
{
    // No prepareToPlay, processBlock, playhead, audio device, or capture.
    Processor offline;
    require(offline.value("syncHost") == 0.0f, "File processing must not auto-start host playback");
    writeWav(folder.getChildFile("reference-48k.wav"), vocal(48000.0, false), 48000.0);
    require(offline.loadFile(folder.getChildFile("reference-48k.wav"), Track::reference), "Offline reference import failed"); waitFor(offline);
    require(offline.loadFile(folder.getChildFile("dub.wav"), Track::dub), "Offline dub import failed"); waitFor(offline);
    require(offline.startAlignment(), "Offline alignment refused"); waitFor(offline);
    require(offline.snapshot()->result != nullptr, "Alignment requires an audio callback");
    require(offline.snapshot()->result->sampleRate == 16000.0, "Result rate must follow the source file");
    require(offline.getSampleRate() == 0.0 && offline.capturedSamples() == 0, "File alignment initialized or captured host audio");
    require(offline.exportWave(folder.getChildFile("offline-result.wav"), Track::result), "Offline export failed");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(folder.getChildFile("offline-result.wav")));
    require(reader && reader->sampleRate == 16000 && reader->lengthInSamples == 56000, "Wrong offline WAV format/duration");
    std::cout << "PASS: 48 kHz reference + 16 kHz dub -> WAV without audio initialization, callbacks, playhead, or capture\n";

    // A file sampled at 16 kHz must not change a running 48 kHz host.
    offline.setRateAndBufferSizeDetails(48000, 64); offline.prepareToPlay(48000, 64);
    offline.setValue("syncHost", 1); require(offline.loadFile(folder.getChildFile("dub.wav"), Track::dub), "Reimport failed"); waitFor(offline);
    require(offline.value("syncHost") == 0 && offline.getSampleRate() == 48000 && offline.captureSampleRate() == 48000,
            "Import changed the host rate or enabled transport playback");
    require(offline.startAlignment(), "Second offline alignment refused"); waitFor(offline);
    require(offline.snapshot()->result->sampleRate == 16000 && offline.getSampleRate() == 48000, "Alignment changed host sample rate");
    offline.startPreview();
    juce::AudioBuffer<float> buffer(2, 64); juce::MidiBuffer midi;
    for (int i = 0; i < 400; ++i) { buffer.clear(); offline.processBlock(buffer, midi); }
    require(offline.playbackSeconds() > 0.53 && offline.playbackSeconds() < 0.54, "16 kHz file played at the wrong speed in a 48 kHz host");
    require(offline.getSampleRate() == 48000 && buffer.getNumSamples() == 64, "Playback changed host rate or frame count");
    std::cout << "PASS: 16 kHz file in 48 kHz host preserves host rate, buffer length, and playback duration\n";
}

static void callbackStress(const juce::File& folder)
{
    constexpr double rate = 48000.0;
    constexpr int frames = 64;
    for (bool dub : { false, true })
    {
        auto phrase = vocal(rate, dub);
        juce::AudioBuffer<float> longAudio(phrase.getNumChannels(), phrase.getNumSamples() * 4);
        for (int repeat = 0; repeat < 4; ++repeat)
            for (int c = 0; c < phrase.getNumChannels(); ++c)
                longAudio.copyFrom(c, repeat * phrase.getNumSamples(), phrase, c, 0, phrase.getNumSamples());
        writeWav(folder.getChildFile(dub ? "stress-dub.wav" : "stress-reference.wav"), longAudio, rate);
    }
    Processor p; p.setRateAndBufferSizeDetails(rate, frames); p.prepareToPlay(rate, frames);
    require(p.loadFile(folder.getChildFile("stress-reference.wav"), Track::reference), "Stress reference import refused"); waitFor(p);
    require(p.loadFile(folder.getChildFile("stress-dub.wav"), Track::dub), "Stress dub import refused"); waitFor(p);
    juce::AudioBuffer<float> buffer(2, frames); juce::MidiBuffer midi;
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(buffer.getWritePointer(c), 0.125f, frames);
    for (int i = 0; i < 32; ++i) p.processBlock(buffer, midi);
    std::vector<double> durations;
    const auto started = std::chrono::steady_clock::now();
    auto next = started;
    require(p.startAlignment(), "Stress alignment refused");
    while (p.isBusy())
    {
        const auto begin = std::chrono::steady_clock::now();
        p.processBlock(buffer, midi);
        const auto end = std::chrono::steady_clock::now();
        durations.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        require(buffer.getSample(0, 0) == 0.125f && buffer.getSample(1, frames - 1) == 0.125f, "Offline render modified host input");
        require(end - started < std::chrono::seconds(30), "Stress worker timed out");
        next += std::chrono::nanoseconds(static_cast<int64_t>(frames / rate * 1.0e9));
        std::this_thread::sleep_until(next);
    }
    require(p.snapshot()->result != nullptr && !durations.empty(), "Stress alignment failed");
    std::sort(durations.begin(), durations.end());
    const double p99 = durations[static_cast<size_t>((durations.size() - 1) * 0.99)];
    const double budget = frames / rate * 1.0e6;
    require(p99 < budget, "P99 callback exceeds its audio buffer budget during offline render");
    std::cout << "PASS: 14-second 48 kHz stereo offline render with " << durations.size() << " concurrent audio callbacks; P99 "
              << p99 << " us / " << budget << " us budget; max " << durations.back() << " us\n";
}

#if JUCE_MAC
void nativeDropChecks(const juce::File&);
#else
static void nativeDropChecks(const juce::File&) {}
#endif

static void regionDropChecks(const juce::File& folder)
{
    const auto reference=folder.getChildFile("promised-reference.aif");
    const auto dub=folder.getChildFile("promised-dub.aif");
    auto writePromise=[&](const juce::File& file,bool isDub)
    {
        const auto source=vocal(48000,isDub);
        juce::AudioBuffer<float> audio(1,163840);audio.copyFrom(0,0,source,0,0,163840);
        auto output=file.createOutputStream();
        require(output!=nullptr,"Synthetic promise output");
        output->setPosition(0);output->truncate();
        std::unique_ptr<juce::OutputStream> stream(std::move(output));
        juce::AiffAudioFormat format;
        auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
        require(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"Synthetic AIFF promise write");
    };
    writePromise(reference,false);writePromise(dub,true);
    Processor p;
    const auto refRequest = p.dropContext(Track::reference), dubRequest = p.dropContext(Track::dub);
    require(p.loadDroppedRegion(reference, refRequest), "Reference region refused"); waitFor(p);
    require(p.isDropContextCurrent(dubRequest), "Independent reference import invalidated pending dub");
    require(p.loadDroppedRegion(dub, dubRequest), "Dub region refused"); waitFor(p);
    require(p.snapshot()->reference && p.snapshot()->dub, "Logic promise regions not loaded");
    for (auto clip : { p.snapshot()->reference, p.snapshot()->dub })
        require(clip->audio.getNumSamples() == 163840 && clip->sampleRate == 48000.0
                && clip->audio.getNumChannels() == 1, "Logic region boundaries/rate/channels changed");
    require(p.snapshot()->anchorSeconds < 0 && p.snapshot()->referenceStartSeconds < 0,
            "Promise import fabricated timeline positions");
    require(p.startAlignment(), "Actual Logic regions alignment refused"); waitFor(p);
    const auto valid = p.snapshot(); require(valid->result != nullptr, "Actual Logic regions alignment failed");
    auto mismatch = vocal(48000.0, false); writeWav(folder.getChildFile("unequal-region.wav"), mismatch, 48000.0);
    p.setValue("syncHost", 1);
    auto context = p.dropContext(Track::dub);
    require(p.loadDroppedRegion(folder.getChildFile("unequal-region.wav"), context), "Mismatch validation not started"); waitFor(p);
    require(p.snapshot() == valid && p.value("syncHost") == 1, "Mismatch overwrote existing pair/result or mode");
    require(p.statusText().contains(juce::String::fromUTF8(u8"длины регионов различаются")), "Mismatch lacks clear error");
    require(p.loadDroppedRegion(folder.getChildFile("missing-region.aif"), context), "Missing file check refused"); waitFor(p);
    require(p.snapshot() == valid, "Failed import changed existing audio/result");
    require(p.newPair() && p.selectPair(valid->id), "Pair switch failed");
    require(!p.loadDroppedRegion(dub, context), "Delayed drop survived switching away and back");
    context = p.dropContext(Track::reference);
    juce::MemoryBlock state; p.getStateInformation(state);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(!p.loadDroppedRegion(reference, context), "Delayed drop survived state restoration");
    require(p.snapshot()->result && p.snapshot()->reference->audio.getNumSamples() == 163840,
            "Imported regions/result not restored");
    context = p.dropContext(Track::dub); p.clearSession();
    require(!p.loadDroppedRegion(dub, context), "Delayed drop survived session clear");
    require(p.loadDroppedRegion(reference, p.dropContext(Track::reference)), "Reference reload refused"); waitFor(p);
    // Equal physical duration across rates, allowing only rounding of one lower-rate sample.
    auto rounded = juce::AudioBuffer<float>(1, 150529); rounded.clear();
    writeWav(folder.getChildFile("rounded-region.wav"), rounded, 44100.0);
    require(p.loadDroppedRegion(folder.getChildFile("rounded-region.wav"), p.dropContext(Track::dub)), "Cross-rate import refused"); waitFor(p);
    require(p.snapshot()->dub && p.snapshot()->dub->audio.getNumSamples() == 150529, "One-sample duration rounding rejected");
    const auto roundedSession = p.snapshot();
    rounded.setSize(1, 150530); rounded.clear(); writeWav(folder.getChildFile("too-long-region.wav"), rounded, 44100.0);
    require(p.loadDroppedRegion(folder.getChildFile("too-long-region.wav"), p.dropContext(Track::dub)), "Duration check refused"); waitFor(p);
    require(p.snapshot() == roundedSession, "Two-sample duration mismatch accepted");
    require(p.loadFile(folder.getChildFile("unequal-region.wav"), Track::dub), "Legacy file import refused"); waitFor(p);
    require(p.snapshot()->dub->duration() == 3.5, "Strict region rule changed existing file import");
    std::cout << "PASS: synthetic AIFF region payloads (163840 frames, 48 kHz mono), render/state, mismatch preserves pair/result/mode, cross-rate rounding, stale contexts, legacy file import\n";

    // Exercise the actual editor callbacks, including asynchronous closure after editor destruction.
    Processor queued; auto editor = std::make_unique<FLOWALIGNAudioProcessorEditor>(queued);
    auto* refDrop = dynamic_cast<flowalign::NativeRegionDrop*>(componentById(*editor, "dropReference"));
    auto* dubDrop = dynamic_cast<flowalign::NativeRegionDrop*>(componentById(*editor, "dropDub"));
    require(refDrop && dubDrop && refDrop->canDrop() && dubDrop->canDrop(), "Native waveform drop zones missing");
    auto finishRef = refDrop->beginDrop(), finishDub = dubDrop->beginDrop();
    finishRef({ reference, {}, {}, true }); finishDub({ dub, {}, {}, true });
    waitFor(queued);
    for (int retry = 0; retry < 20 && !queued.snapshot()->dub; ++retry)
    { juce::Thread::sleep(50); juce::Timer::callPendingTimersSynchronously(); waitFor(queued); }
    require(queued.snapshot()->reference && queued.snapshot()->dub, "Two quick editor drops lost a signal");
    auto late = refDrop->beginDrop(); editor.reset();
    const auto beforeLate = queued.snapshot(); late({ reference, {}, {}, true });
    require(queued.snapshot() == beforeLate, "Closed editor applied a late promise");
    std::cout << "PASS: two immediate editor drops queued, late callback after editor destruction discarded\n";
    nativeDropChecks(folder);
}

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        require(argc == 2, "Supply output directory");
        const auto folder = juce::File(argv[1]); folder.createDirectory();
        Processor p;
        Processor::TrackProperties properties; properties.name = juce::String::fromUTF8(u8"Дубль — припев"); p.updateTrackProperties(properties);
        p.setRateAndBufferSizeDetails(16000, 128); p.prepareToPlay(16000, 128);
        snapshot(p, folder, "editor-empty.png");
        for (auto style : { juce::Font::plain, juce::Font::bold })
        {
            juce::Font font(juce::FontOptions("Arial", 15.0f, style));
            const auto face = font.getTypefacePtr();
            for (auto cp : juce::String::fromUTF8(u8"АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя"))
                require(face->getNominalGlyphForCodepoint(cp).value_or(0) != 0, "Missing Cyrillic glyph");
        }
        std::cout << "PASS: Cyrillic font and empty editor screenshot\n";
        defaultSettingsChecks(); captureWithoutEditorChecks(); audioChecks(); transportChecks(); captureWorkflowChecks(folder); legacyStateChecks(folder); playbackTransferChecks(folder); referenceRoutingChecks(); regionDropChecks(folder);
        constexpr double rate = 16000;
        auto ref = vocal(rate, false), dub = vocal(rate, true);
        std::atomic<float> progress { 0 }; std::atomic<bool> cancelled { false };
        flowalign::Settings options { 1.0f, 0.2f, 0.8f, true, true, false };
        auto result = flowalign::align(ref, dub, rate, options, progress, cancelled);
        double beforeError = 0, afterError = 0;
        for (double event : { 0.5, 1.25, 2.1, 2.85 })
        {
            const double before = energyPeak(dub, rate, event), after = energyPeak(result.audio, rate, event);
            beforeError += std::abs(before - event); afterError += std::abs(after - event);
            std::cout << "Timing event " << event << ": before=" << before << " after=" << after << '\n';
        }
        std::cout << "Timing mean error: " << beforeError * 250 << " ms -> " << afterError * 250 << " ms\n";
        require(afterError < beforeError * 0.5, "Alignment did not improve timing enough");
        for (int i = 0; i < result.audio.getNumSamples(); ++i)
        {
            require(std::isfinite(result.audio.getSample(0, i)), "Non-finite output");
            require(std::abs(result.audio.getSample(1, i) + 0.7f * result.audio.getSample(0, i)) < 0.000002f, "Stereo phase changed");
        }
        for (size_t i = 1; i < result.sourceSeconds.size(); ++i)
        {
            const double delta = result.sourceSeconds[i] - result.sourceSeconds[i - 1];
            require(delta >= 0.01 / 1.8 - 0.00001 && delta <= 0.018 + 0.00001, "Stretch bound violated");
        }
        int crossings = 0; const int first = static_cast<int>(0.46 * rate), last = static_cast<int>(0.54 * rate);
        for (int i = first + 1; i < last; ++i)
            if (result.audio.getSample(0, i - 1) <= 0 && result.audio.getSample(0, i) > 0) ++crossings;
        const double frequency = crossings / 0.08;
        std::cout << "Pitch estimate: " << frequency << " Hz (source 223 Hz)\n";
        require(std::abs(frequency - 223.0) < 18.0, "Pitch changed excessively");
        options.tightness = 0.0f;
        auto identity = flowalign::align(ref, dub, rate, options, progress, cancelled);
        for (int c = 0; c < 2; ++c)
            require(std::memcmp(identity.audio.getReadPointer(c), dub.getReadPointer(c), dub.getNumSamples() * sizeof(float)) == 0, "0% tightness must preserve original");
        juce::AudioBuffer<float> silence(1, 16000); silence.clear();
        bool rejected = false;
        try { flowalign::align(silence, dub, rate, options, progress, cancelled); } catch (...) { rejected = true; }
        require(rejected, "Silent reference should be rejected");
        std::cout << "PASS: DTW timing, stretch limit, pitch, stereo phase, 0% identity, silence detection\n";
        auto highRate = vocal(48000.0, false);
        auto converted = flowalign::resample(highRate, 48000.0, 44100.0);
        require(converted.getNumSamples() == static_cast<int>(3.5 * 44100), "Resampling changed duration");
        require(std::abs(energyPeak(converted, 44100.0, 0.5) - 0.5) < 0.008, "Resampling added a timing offset");
        cancelled.store(true); rejected = false;
        try { flowalign::align(ref, dub, rate, options, progress, cancelled); } catch (...) { rejected = true; }
        require(rejected, "Cancellation was ignored"); cancelled.store(false);
        std::cout << "PASS: 48 -> 44.1 kHz resampling duration/timing, cancellation\n";
        writeWav(folder.getChildFile("reference.wav"), ref, rate); writeWav(folder.getChildFile("dub.wav"), dub, rate);
        fileOnlyChecks(folder); callbackStress(folder);
        require(p.loadFile(folder.getChildFile("reference.wav"), Track::reference), "Reference import refused"); waitFor(p);
        require(p.loadFile(folder.getChildFile("dub.wav"), Track::dub), "Dub import refused"); waitFor(p);
        require(p.snapshot()->reference && p.snapshot()->dub, "Imports missing");
        p.setValue("tightness", 1); p.setValue("naturalness", 0.2f); p.setValue("maxStretch", 0.8f);
        require(p.startAlignment(), "Alignment refused"); waitFor(p); require(p.snapshot()->result != nullptr, "Background alignment failed");
        require(p.exportWave(folder.getChildFile("aligned.wav"), Track::result), "WAV export failed");
        require(p.exportTiming(folder.getChildFile("timing.csv")), "Timing export failed");
        p.startPreview(); juce::AudioBuffer<float> buffer(2, 128); juce::MidiBuffer midi;
        for (int block = 0; block < 80; ++block) { buffer.clear(); p.processBlock(buffer, midi); }
        require(p.playbackSeconds() > 0.5 && p.outputLevel() > 0.0f, "Preview did not play audio");
        p.stopPreview();
        p.setValue("bypass", 1); p.startPreview();
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(buffer.getWritePointer(c), 0.125f, 128);
        p.processBlock(buffer, midi); require(buffer.getSample(0, 100) == 0.125f, "Bypass changed audio");
        p.stopPreview(); p.setValue("bypass", 0);
        TestPlayHead host; host.samples = 160000; host.playing = true; p.setPlayHead(&host);
        buffer.clear(); p.processBlock(buffer, midi);
        require(p.anchorToHost(), "Cannot anchor imported audio to host");
        p.setValue("syncHost", 1);
        host.samples += 8000; buffer.clear(); p.processBlock(buffer, midi);
        require(std::abs(buffer.getSample(0, 10) - p.snapshot()->result->audio.getSample(0, 8010)) < 0.00001f,
                "Result is not synchronized with host transport");
        p.setValue("after", 0);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.125f, 128); p.processBlock(buffer, midi);
        require(buffer.getSample(0, 10) == 0.125f, "Before mode must pass original host input");
        p.setValue("after", 1); host.samples += 160000;
        juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.125f, 128); p.processBlock(buffer, midi);
        require(buffer.getSample(0, 10) == 0.125f, "Audio outside captured region must remain unchanged");
        host.samples = 160000;
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(buffer.getWritePointer(c), 0.125f, 128);
        p.processBlock(buffer, midi);
        require(buffer.getSample(0, 0) == 0.125f, "Region start has a discontinuous replacement");
        require(std::abs(buffer.getSample(0, 100) - p.snapshot()->result->audio.getSample(0, 100)) < 0.00001f,
                "Boundary fade altered the interior of the result");
        host.samples = 160000 + p.snapshot()->result->audio.getNumSamples() - 1;
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(buffer.getWritePointer(c), 0.125f, 128);
        p.processBlock(buffer, midi);
        require(std::abs(buffer.getSample(0, 0) - 0.125f) < 0.005f && buffer.getSample(0, 1) == 0.125f,
                "Region end does not return smoothly to the input");
        p.setPlayHead(nullptr); p.seek(0);
        std::cout << "PASS: imported audio anchor, synchronized result, before mode, passthrough outside region\n";
        juce::MemoryBlock state; p.getStateInformation(state);
        Processor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(restored.snapshot()->result && restored.snapshot()->dub && restored.snapshot()->reference, "Session audio not restored");
        require(restored.value("tightness") == 1.0f, "Parameters not restored");
        require(std::memcmp(restored.snapshot()->result->audio.getReadPointer(0), p.snapshot()->result->audio.getReadPointer(0),
                           p.snapshot()->result->audio.getNumSamples() * sizeof(float)) == 0, "State changed audio samples");
        const auto restoredRevision = restored.snapshot()->revision;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize() / 2));
        require(restored.snapshot()->revision == restoredRevision, "Truncated session overwrote valid audio");
        p.setValue("tightness",1);p.setValue("naturalness",1);p.setValue("maxStretch",0.3f);
        require(p.startAlignment(),"Default settings screenshot render refused");waitFor(p);
        snapshot(p, folder, "editor-result.png"); snapshot(p, folder, "editor-result-small.png", 1000);
        std::cout << "PASS: file import, background alignment, WAV/CSV export, preview, bypass, lossless session restore\nAll checks PASS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
