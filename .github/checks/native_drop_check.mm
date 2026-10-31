#import <Cocoa/Cocoa.h>
#include "../../Source/NativeRegionDrop.h"
#include <iostream>
#include <stdexcept>

// Controlled source at the AppKit boundary: no GUI automation or pasteboard server.
@interface FAVerificationReceiver : NSFilePromiseReceiver
@property(nonatomic, copy) void (^pending)(NSURL*, NSError*);
@property(nonatomic, retain) NSURL* destination;
@property(nonatomic, retain) NSOperationQueue* operationQueue;
@property(nonatomic) NSUInteger count;
- (void)finish:(BOOL)fail;
@end
@implementation FAVerificationReceiver
- (NSArray*)fileTypes { return self.count == 1 ? @[@"public.aiff-audio"] : @[@"public.aiff-audio", @"public.aiff-audio"]; }
- (NSArray*)fileNames { return self.count == 1 ? @[@"region.aif"] : @[@"one.aif", @"two.aif"]; }
- (void)receivePromisedFilesAtDestination:(NSURL*)destination options:(NSDictionary*)options
    operationQueue:(NSOperationQueue*)queue reader:(void (^)(NSURL*, NSError*))reader
{
    (void)options; self.operationQueue = queue; self.destination = destination; self.pending = reader;
}
- (void)finish:(BOOL)fail
{
    NSURL* url = [self.destination URLByAppendingPathComponent:@"region.aif"];
    if (!fail) [@"region bytes" writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:nil];
    void (^callback)(NSURL*, NSError*) = [self.pending copy]; self.pending = nil;
    callback(url, fail ? [NSError errorWithDomain:@"Verification" code:1 userInfo:nil] : nil);
    [callback release];
}
- (void)dealloc { self.pending = nil; self.destination = nil; self.operationQueue = nil; [super dealloc]; }
@end

@interface FAVerificationPasteboard : NSObject
@property(nonatomic, retain) NSArray* promises;
@property(nonatomic, retain) NSArray* urls;
@property(nonatomic) BOOL logicRegion;
- (NSArray*)types;
- (NSArray*)readObjectsForClasses:(NSArray*)classes options:(NSDictionary*)options;
@end
@implementation FAVerificationPasteboard
- (NSArray*)types { return self.logicRegion ? @[@"com.apple.musicapps.LGRegionsPboardType"] : @[NSPasteboardTypeFileURL]; }
- (NSArray*)readObjectsForClasses:(NSArray*)classes options:(NSDictionary*)options
{
    (void)options;
    return [classes containsObject:[NSFilePromiseReceiver class]] ? self.promises : self.urls;
}
- (void)dealloc { self.promises = nil; self.urls = nil; [super dealloc]; }
@end
@interface FAVerificationDrag : NSObject
@property(nonatomic, retain) FAVerificationPasteboard* draggingPasteboard;
@end
@implementation FAVerificationDrag
- (void)dealloc { self.draggingPasteboard = nil; [super dealloc]; }
@end

static void verify(bool condition, const char* error)
{ if (!condition) throw std::runtime_error(error); }

void nativeDropChecks(const juce::File& folder)
{
    @autoreleasepool
    {
        auto* receiver = [[[FAVerificationReceiver alloc] init] autorelease]; receiver.count = 1;
        auto* pb = [[[FAVerificationPasteboard alloc] init] autorelease]; pb.promises = @[receiver];
        // This URL is deliberately wrong: a promise must win over a whole-take URL.
        pb.urls = @[[NSURL fileURLWithPath:@"/wrong-whole-take.aif"]];
        auto* info = [[[FAVerificationDrag alloc] init] autorelease]; info.draggingPasteboard = pb;
        auto drop = std::make_unique<flowalign::NativeRegionDrop>();
        flowalign::RegionDrop received;
        int completions = 0; bool allowed = true, deliveredOnMain = false;
        drop->canDrop = [&] { return allowed; };
        drop->beginDrop = [&]() -> flowalign::NativeRegionDrop::Completion
        { return [&](flowalign::RegionDrop data) { ++completions; deliveredOnMain = [NSThread isMainThread]; received = std::move(data); }; };
        auto* view = static_cast<NSView*>(drop->getView());
        auto* destination = static_cast<id<NSDraggingDestination>>(view);
        auto* drag = static_cast<id<NSDraggingInfo>>(info);
        verify([destination draggingEntered:drag] == NSDragOperationCopy, "Native drag not accepted");
        allowed = false;
        verify([destination draggingUpdated:drag] == NSDragOperationNone, "Disabled native drop accepted");
        allowed = true;
        verify([destination performDragOperation:drag] && completions == 0, "Promise read before completion");
        [receiver finish:NO];
        verify(completions == 1 && received.promised && received.error.isEmpty() && received.file.existsAsFile(),
               "Native promise delivery failed or chose whole-take URL");
        const auto temporary = received.file.getParentDirectory();
        verify(temporary.exists(), "Temporary promise deleted before consumption");
        received = {};
        verify(!temporary.exists(), "Private promise destination not cleaned up");
        verify([destination performDragOperation:drag], "Background promise refused");
        [receiver.operationQueue addOperationWithBlock:^{ [receiver finish:NO]; }];
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
        while (completions < 2 && juce::Time::getMillisecondCounterHiRes() < deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
        verify(completions == 2 && deliveredOnMain && received.file.existsAsFile(), "Background reader did not deliver on main");
        received = {};
        verify([destination performDragOperation:drag], "Second promise refused");
        [receiver finish:YES];
        verify(received.error.isNotEmpty(), "Promise source error not reported"); received = {};
        receiver.count = 2;
        verify(![destination performDragOperation:drag] && received.error.isNotEmpty(), "Multiple promised files accepted");
        receiver.count = 1; received = {};
        // The reader must not dereference a view destroyed while Logic produces the file.
        verify([destination performDragOperation:drag], "Late promise refused"); drop.reset();
        [receiver finish:NO]; verify(received.file.existsAsFile(), "Late reader lost file lease"); received = {};
        pb.promises = @[];
        const auto source = folder.getChildFile("finder-source.wav"); source.replaceWithText("original");
        pb.urls = @[[NSURL fileURLWithPath:[NSString stringWithUTF8String:source.getFullPathName().toRawUTF8()]]];
        flowalign::NativeRegionDrop finder;
        finder.canDrop = [] { return true; };
        finder.beginDrop = [&]() -> flowalign::NativeRegionDrop::Completion
        { return [&](flowalign::RegionDrop data) { received = std::move(data); }; };
        destination = static_cast<id<NSDraggingDestination>>(static_cast<NSView*>(finder.getView()));
        verify([destination performDragOperation:drag] && !received.promised && received.file == source,
               "Ordinary Finder URL import changed"); received = {};
        verify(source.existsAsFile(), "Native drop removed source file");
        pb.logicRegion = YES;
        verify(![destination performDragOperation:drag] && received.error.isNotEmpty(), "Logic region silently fell back to full-take URL");
    }
    std::cout << "PASS: native AppKit adapter: async promise preferred over URL, single-file gate, source error, view destruction, temporary lease cleanup, Finder source preserved\n";
}
