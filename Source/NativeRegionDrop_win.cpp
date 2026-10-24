#include "NativeRegionDrop.h"

namespace flowalign
{
// JUCE's Windows OLE receiver accepts CF_HDROP files from Explorer and DAWs
// which materialise dragged audio (for example, the Edison drag/copy handle).
// Private DAW clip objects are not audio files and are not interpreted as such.
NativeRegionDrop::NativeRegionDrop() { setWantsKeyboardFocus(false); }
NativeRegionDrop::~NativeRegionDrop() = default;
bool NativeRegionDrop::isInterestedInFileDrag(const juce::StringArray& files)
{
    return !files.isEmpty() && (!canDrop || canDrop());
}
void NativeRegionDrop::fileDragEnter(const juce::StringArray&, int, int)
{ if (hover) hover(true); }
void NativeRegionDrop::fileDragExit(const juce::StringArray&)
{ if (hover) hover(false); }
void NativeRegionDrop::filesDropped(const juce::StringArray& files, int, int)
{
    if (hover) hover(false);
    if ((canDrop && !canDrop()) || !beginDrop) return;
    auto complete = beginDrop();
    if (!complete) return;
    RegionDrop result;
    result.checkLength = true;
    if (files.size() != 1)
        result.error = juce::String::fromUTF8(u8"Переносите по одному аудиофайлу на каждую волну");
    else
    {
        result.file = juce::File(files[0]);
        if (!result.file.existsAsFile())
            result.error = juce::String::fromUTF8(u8"Секвенсор не передал доступный аудиофайл. Используйте перенос аудио из редактора или захват через Side Chain");
    }
    complete(std::move(result));
}
void NativeRegionDrop::mouseDown(const juce::MouseEvent& event)
{ if (seekAt) seekAt(event.position.x); }
}
