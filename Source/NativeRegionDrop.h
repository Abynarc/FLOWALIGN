#pragma once
#include <JuceHeader.h>

namespace flowalign
{
// The lease owns only a private temporary destination, never the source in Logic.
struct RegionDrop
{
    juce::File file;
    std::shared_ptr<void> lease;
    juce::String error;
    bool promised = false;
    bool checkLength = false;
};

class NativeRegionDrop final
#if JUCE_MAC
    : public juce::NSViewComponent
#else
    : public juce::Component, public juce::FileDragAndDropTarget
#endif
{
public:
#if JUCE_WINDOWS
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void mouseDown(const juce::MouseEvent&) override;
#endif
    using Completion = std::function<void(RegionDrop)>;
    NativeRegionDrop();
    ~NativeRegionDrop() override;
    // Called at drop time, before asynchronous delivery, to capture the target pair.
    std::function<Completion()> beginDrop;
    std::function<bool()> canDrop;
    std::function<void(bool)> hover;
    std::function<void(float)> seekAt;
};
}
