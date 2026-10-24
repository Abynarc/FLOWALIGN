#import <Cocoa/Cocoa.h>
#include "NativeRegionDrop.h"

namespace
{
struct TemporaryRegion
{
    juce::File directory;
    NSOperationQueue* queue = [[NSOperationQueue alloc] init];
    TemporaryRegion() { queue.qualityOfService = NSQualityOfServiceUtility; }
    ~TemporaryRegion() { directory.deleteRecursively(); [queue release]; }
};
}

@interface FLOWALIGNRegionDropView : NSView <NSDraggingDestination>
{
@public
    flowalign::NativeRegionDrop* bridge;
}
@end

@implementation FLOWALIGNRegionDropView
- (id)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        NSMutableArray* types = [NSMutableArray arrayWithArray:[NSFilePromiseReceiver readableDraggedTypes]];
        [types addObject:NSPasteboardTypeFileURL];
        [self registerForDraggedTypes:types];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstResponder { return NO; }
- (void)mouseDown:(NSEvent*)event
{
    if (bridge && bridge->seekAt)
        bridge->seekAt(static_cast<float>([self convertPoint:event.locationInWindow fromView:nil].x));
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    (void)sender;
    const bool accepted = bridge && bridge->canDrop && bridge->canDrop();
    if (bridge && bridge->hover) bridge->hover(accepted);
    return accepted ? NSDragOperationCopy : NSDragOperationNone;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return [self draggingEntered:sender]; }
- (void)draggingExited:(id<NSDraggingInfo>)sender
{
    (void)sender;
    if (bridge && bridge->hover) bridge->hover(false);
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender
{
    (void)sender;
    return bridge && bridge->canDrop && bridge->canDrop();
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    if (!bridge || !bridge->beginDrop || !bridge->canDrop || !bridge->canDrop()) return NO;
    if (bridge->hover) bridge->hover(false);
    const auto complete = bridge->beginDrop();
    if (!complete) return NO;
    NSPasteboard* pasteboard = sender.draggingPasteboard;
    // Logic promises a newly rendered region. A source URL may refer to the full take.
    NSArray* receivers = [pasteboard readObjectsForClasses:@[[NSFilePromiseReceiver class]] options:nil];
    if (receivers.count > 0)
    {
        NSFilePromiseReceiver* receiver = receivers.firstObject;
        if (receivers.count != 1 || receiver.fileTypes.count != 1)
        {
            complete({ {}, {}, juce::String::fromUTF8(u8"Перетащите один аудиорегион за раз"), true });
            return NO;
        }
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("FLOWALIGN-region-drop").getChildFile(juce::Uuid().toString());
        if (directory.createDirectory().failed())
        {
            complete({ {}, {}, juce::String::fromUTF8(u8"Не удалось создать временную папку для региона"), true });
            return NO;
        }
        auto lease = std::make_shared<TemporaryRegion>(); lease->directory = directory;
        auto delivered = std::make_shared<std::atomic<bool>>(false);
        NSURL* destination = [NSURL fileURLWithPath:[NSString stringWithUTF8String:directory.getFullPathName().toRawUTF8()] isDirectory:YES];
        [receiver receivePromisedFilesAtDestination:destination options:@{} operationQueue:lease->queue
            reader:^(NSURL* url, NSError* error)
        {
            if (delivered->exchange(true)) return;
            flowalign::RegionDrop drop; drop.promised = true; drop.lease = lease;
            if (error)
                drop.error = juce::String::fromUTF8(u8"Logic не передал регион: ")
                    + juce::String::fromUTF8(error.localizedDescription.UTF8String);
            else if (!url.isFileURL || receiver.fileNames.count != 1)
                drop.error = juce::String::fromUTF8(u8"Нужен один готовый аудиофайл региона");
            else
            {
                drop.file = juce::File(juce::String::fromUTF8(url.path.UTF8String));
                if (!drop.file.isAChildOf(directory))
                    drop.error = juce::String::fromUTF8(u8"Logic передал файл вне папки приёма");
            }
            // File coordination may wait on the source. Only the final UI callback runs on main.
            if ([NSThread isMainThread]) complete(std::move(drop));
            else dispatch_async(dispatch_get_main_queue(), ^{ complete(drop); });
        }];
        return YES;
    }
    if ([pasteboard.types containsObject:@"com.apple.musicapps.LGRegionsPboardType"]
        || [pasteboard.types containsObject:@"com.apple.logic.eventstype"])
    {
        complete({ {}, {}, juce::String::fromUTF8(u8"Logic не передал готовый файл региона. Повторите перенос или используйте запись"), true });
        return NO;
    }
    NSArray* urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if (urls.count != 1)
    {
        complete({ {}, {}, juce::String::fromUTF8(u8"Перетащите один аудиофайл или регион"), false });
        return NO;
    }
    NSURL* url = urls.firstObject;
    complete({ juce::File(juce::String::fromUTF8(url.path.UTF8String)), {}, {}, false });
    return YES;
}
@end

namespace flowalign
{
NativeRegionDrop::NativeRegionDrop()
{
    auto* view = [[FLOWALIGNRegionDropView alloc] initWithFrame:NSZeroRect];
    view->bridge = this; setView(view); [view release];
}
NativeRegionDrop::~NativeRegionDrop()
{
    if (auto* view = static_cast<FLOWALIGNRegionDropView*>(getView())) view->bridge = nullptr;
    setView(nullptr);
}
}
