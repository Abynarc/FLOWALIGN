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
};

class NativeRegionDrop final : public juce::NSViewComponent
{
public:
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
