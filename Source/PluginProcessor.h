#pragma once
#include <JuceHeader.h>
#include "AlignmentEngine.h"
#include "SystemPreview.h"
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

class FLOWALIGNAudioProcessor final : public juce::AudioProcessor
{
public:
    enum class Track { reference, dub, result };
    enum class CaptureMode { both, reference, dub };
    struct Peak { float low = 0.0f, high = 0.0f; };
    struct Clip
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        juce::String name;
        std::vector<Peak> peaks;
        double duration() const { return audio.getNumSamples() / sampleRate; }
    };
    struct Pair
    {
        std::shared_ptr<const Clip> reference, dub, result;
        double anchorSeconds = -1.0, referenceStartSeconds = -1.0, correctionMs = 0.0;
        int id = 0;
        juce::String resultId;
        flowalign::Settings renderedSettings;
        bool hasRenderedSettings = false;
        std::vector<double> sourceSeconds;
    };
    struct Session : Pair
    {
        std::vector<Pair> otherPairs;
        uint64_t revision = 0;
    };
    struct DropContext
    {
        Track track = Track::reference;
        int pairId = 0;
        uint64_t epoch = 0;
        std::shared_ptr<const Clip> previous;
    };

    FLOWALIGNAudioProcessor();
    ~FLOWALIGNAudioProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FLOWALIGN"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void updateTrackProperties(const TrackProperties&) override;

    juce::AudioProcessorValueTreeState parameters;
    float value(const char* id) const { return parameters.getRawParameterValue(id)->load(); }
    void setValue(const char* id, float actual);
    flowalign::Settings settings() const;
    bool startCapture(CaptureMode = CaptureMode::both);
    void stopCapture();
    void pollCapture(); // Message thread: publishes a stopped capture after the audio reader exits.
    bool isCapturing() const { return captureRequested.load(); }
    int capturedSamples() const { return capturedCount.load(); }
    double captureSampleRate() const { return captureRate.load(); }
    bool isCaptureRecording() const { return recording.load(); }
    CaptureMode captureMode() const { return static_cast<CaptureMode>(requestedCaptureMode.load()); }
    bool isCapturingTrack(Track track) const;
    bool isCapturePending() const { return capturePending.load(); }
    float getPeak(bool reference) const { return (reference ? referencePeak : dubPeak).load(); }
    float outputLevel() const { return systemPreviewLoaded ? systemPreview.peak() : outputPeak.load(); }
    bool hasSidechain() const { return sidechainPresent.load(); }
    // An enabled AU bus does not prove that Logic routed a different track.
    bool referenceMirrorsDub() const { return captureReferenceMirrorsDub.load(); }
    std::vector<Peak> liveWaveform(bool reference) const;
    std::shared_ptr<const Session> snapshot() const;
    juce::String trackName() const;
    juce::String statusText() const;
    bool loadFile(const juce::File&, Track);
    DropContext dropContext(Track) const;
    bool isDropContextCurrent(const DropContext&) const;
    bool loadDroppedRegion(const juce::File&, const DropContext&, std::shared_ptr<void> lease = {});
    bool startAlignment();
    void cancelJob() { cancelled.store(true); }
    bool isBusy() const { return busy.load(); }
    float jobProgress() const { return progress.load(); }
    int jobStage() const { return stage.load(); }
    void startPreview();
    bool startPreview(Track);
    bool makePreviewWave(Track, juce::MemoryBlock&) const;
    bool startSystemPreview(Track, double position = 0.0);
    void pollPreview();
    Track previewTrack() const { return static_cast<Track>(selectedPreviewTrack.load()); }
    void selectPreviewTrack(Track track) { selectedPreviewTrack.store(static_cast<int>(track)); seek(0.0); }
    void stopPreview();
    bool isPreviewing() const { return systemPreviewLoaded ? systemPreview.isPlaying() : previewing.load(); }
    double playbackSeconds() const { return systemPreviewLoaded ? systemPreview.position() : playbackPosition.load(); }
    void seek(double seconds);
    bool anchorToHost();
    bool isHostPlaying() const { return hostPlaying.load(); }
    bool hasHostPosition() const { return hostPositionKnown.load(); }
    bool exportWave(const juce::File&, Track) const;
    bool exportTiming(const juce::File&) const;
    bool newPair();
    bool selectPair(int id);
    bool hasCurrentResult() const;
    juce::StringArray pairNames() const;
    // Persistent, automatically named audio for external drag; no save dialog.
    juce::File prepareResultForLogic(const juce::File& storage = {});
    void clearSession();
    static std::shared_ptr<Clip> makeClip(juce::AudioBuffer<float>, double, juce::String);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    void publish(std::shared_ptr<Session>);
    void setStatus(juce::String);
    bool launch(std::function<void()>);
    bool loadAudioFile(const juce::File&, Track, std::shared_ptr<const DropContext>, std::shared_ptr<void>);
    static const Clip* clipFor(const Pair&, Track);
    static bool sameSettings(const flowalign::Settings&, const flowalign::Settings&);
    static size_t audioBytes(const Session&);
    static float readSample(const Clip&, int channel, double seconds);

    // The audio thread uses a raw immutable session under a reader epoch. Previous
    // sessions are reclaimed on a non-audio thread only after all readers exit.
    mutable std::mutex modelMutex, captureMutex, jobMutex;
    std::shared_ptr<const Session> current;
    std::vector<std::shared_ptr<const Session>> retired;
    std::atomic<const Session*> realtimeSession { nullptr };
    std::atomic<int> audioReaders { 0 };
    juce::String hostTrackName, status;
    std::thread worker;
    std::atomic<bool> busy { false }, cancelled { false };
    std::atomic<uint64_t> dropEpoch { 0 };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> stage { 0 };
    juce::AudioBuffer<float> captureDub, captureReference;
    std::atomic<bool> captureRequested { false }, capturePending { false }, recording { false };
    std::atomic<int> requestedCaptureMode { 0 };
    std::atomic<uint64_t> captureGeneration { 0 };
    uint64_t activeCaptureGeneration = 0;
    int capturePosition = 0;
    double captureStartSeconds = -1.0;
    int64_t expectedHostSample = 0;
    bool captureHasHostTime = false;
    std::atomic<bool> captureReferenceMirrorsDub { false };
    bool captureComparedReference = false, captureReferenceDifferent = false;
    std::atomic<int> capturedCount { 0 };
    std::atomic<double> captureRate { 44100.0 }, hostSeconds { 0.0 }, playbackPosition { 0.0 }, previewCursor { 0.0 };
    std::atomic<uint64_t> seekGeneration { 0 };
    std::atomic<bool> previewing { false }, hostPlaying { false }, hostPositionKnown { false }, sidechainPresent { false };
    std::atomic<int> selectedPreviewTrack { static_cast<int>(Track::dub) };
    flowalign::SystemPreview systemPreview;
    std::atomic<bool> systemPreviewLoaded { false };
    bool previewFinishedNotified = false;
    std::atomic<float> referencePeak { 0.0f }, dubPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<float>* rawAfter = nullptr;
    std::atomic<float>* rawBypass = nullptr;
    std::atomic<float>* rawLoop = nullptr;
    std::atomic<float>* rawSync = nullptr;
    static constexpr int historySize = 512;
    std::array<std::atomic<float>, historySize> liveReference {}, liveDub {};
    std::atomic<unsigned> historyCursor { 0 };
    int meterSamples = 0;
    float accumulatedReference = 0.0f, accumulatedDub = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLOWALIGNAudioProcessor)
};
