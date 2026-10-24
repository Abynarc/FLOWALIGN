#pragma once
#include "PluginProcessor.h"
#include "NativeRegionDrop.h"
#include <deque>

class FlowLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
};

class FLOWALIGNAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer,
                                          public juce::FileDragAndDropTarget,
                                          public juce::DragAndDropContainer
{
public:
    explicit FLOWALIGNAudioProcessorEditor(FLOWALIGNAudioProcessor&);
    ~FLOWALIGNAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void openAudio(FLOWALIGNAudioProcessor::Track);
    void sourceMenu(FLOWALIGNAudioProcessor::Track, juce::Component*);
    void showHelp();
    void dragResult();
    void togglePreview(FLOWALIGNAudioProcessor::Track);
    void revealResult();
    void transferHint();
private:
    class WavePanel;
    class AlignButton;
    class DragButton;
    void timerCallback() override;
    void exportAudio(FLOWALIGNAudioProcessor::Track);
    void saveTiming();
    void applyPreset(int);
    void selectProfile(int);
    void updateControls();
    void receiveDrop(flowalign::RegionDrop, FLOWALIGNAudioProcessor::DropContext);
    void pollDrops();
    void dropFeedback(juce::String);
    struct PendingDrop
    {
        flowalign::RegionDrop data;
        FLOWALIGNAudioProcessor::DropContext context;
    };
    std::deque<PendingDrop> pendingDrops;
    juce::Rectangle<int> scaled(int, int, int, int) const;
    FLOWALIGNAudioProcessor& engine;
    FlowLookAndFeel look;
    juce::TooltipWindow tooltips { this, 500 };
    std::unique_ptr<WavePanel> referenceWave, dubWave, resultWave;
    std::unique_ptr<AlignButton> align;
    std::unique_ptr<DragButton> drag;
    juce::TextButton capture, play, stop, loop, before, after, bypass, link, settingsButton;
    juce::TextButton previousPreset, nextPreset, profileA, profileB, localMode, hostMode, saveResult, saveOriginal;
    juce::ComboBox presets, captureTarget, pairs;
    juce::TextButton addPair;
    juce::Slider tightness, naturalness, maxStretch, zoom;
    juce::ToggleButton preservePitch, protectTransients, autoGain;
    juce::Label status;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> chooser;
    std::array<flowalign::Settings, 2> profiles;
    int activeProfile = 0;
    uint64_t lastRevision = 0;
    uint64_t pairRevision = 0;
    juce::String feedback;
    double feedbackUntil = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLOWALIGNAudioProcessorEditor)
};
