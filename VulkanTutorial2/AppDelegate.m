#import <QuartzCore/CAMetalLayer.h>

#import "AppDelegate.h"
#import "ViewController.h"

#include "VulkanTutorial2.hpp"

@interface AppDelegate ()

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    ViewController* viewController = (ViewController*)[[NSApplication sharedApplication] mainWindow].contentViewController;
    NSView* view = [viewController view];
    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        [view setLayer:[CAMetalLayer layer]];
        [view setWantsLayer:YES];
    }
    
    NSSize size = [view frame].size;
    
    vktut2_create((__bridge void *)(view), (int)size.width, (int)size.height);
}


- (void)applicationWillTerminate:(NSNotification *)aNotification
{
    vktut2_destroy();
}


@end
