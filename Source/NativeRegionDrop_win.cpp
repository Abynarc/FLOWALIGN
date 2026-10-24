#include "NativeRegionDrop.h"

namespace flowalign
{
namespace
{
struct TemporaryAudio
{
    juce::File folder;
    ~TemporaryAudio() { folder.deleteRecursively(); }
};
}

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
        const auto source = result.file;
        if (!source.existsAsFile())
            result.error = juce::String::fromUTF8(u8"Секвенсор не передал доступный аудиофайл. Используйте перенос аудио из редактора или захват через Side Chain");
        else if (source.getSize() <= 0 || source.getSize() > 256LL * 1024 * 1024)
            result.error = juce::String::fromUTF8(u8"Аудиофайл пуст или превышает 256 МБ");
        else
        {
            // A DAW may remove its temporary export after Drop returns. Copy before
            // returning to OLE; retain only our own destination until decoding ends.
            auto lease = std::make_shared<TemporaryAudio>();
            lease->folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("FLOWALIGN-region-drop").getChildFile(juce::Uuid().toString());
            const auto destination = lease->folder.getChildFile(source.getFileName());
            if (lease->folder.createDirectory().failed() || !source.copyFileTo(destination))
                result.error = juce::String::fromUTF8(u8"Не удалось принять аудиофайл во временную папку");
            else { result.file = destination; result.lease = std::move(lease); }
        }
    }
    complete(std::move(result));
}
void NativeRegionDrop::mouseDown(const juce::MouseEvent& event)
{ if (seekAt) seekAt(event.position.x); }
}
