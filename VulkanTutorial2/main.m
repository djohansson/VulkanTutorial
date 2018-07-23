#import <Cocoa/Cocoa.h>
#include "AppDelegate.h"



int main(int argc, const char * argv[])
{
    return NSApplicationMain(argc, argv);
    /*
    @autoreleasepool
    {
        //[[NSAutoreleasePool alloc] init];
        [NSApplication sharedApplication];
        [NSApp setDelegate:[[AppDelegate alloc] init]];
        //[NSBundle loadNibNamed:@"MainMenu" owner:[NSApp delegate]];
        [NSApp finishLaunching];
        NSRect frame = NSMakeRect(0, 0, 200, 200);
        NSWindow* window  = [[NSWindow alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        [window setBackgroundColor:[NSColor blueColor]];
        [window makeKeyAndOrderFront:NSApp];
        
        bool quit = false;
        while (!quit)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES];
            switch([(NSEvent*)event type])
            {
                case NSEventTypeKeyDown:
                    quit = true;
                    break;
                default:
                    [NSApp sendEvent:event];
                    break;
            }
            [NSApp run];
        }
    }
    return 0;
     */
}
