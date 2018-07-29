#import "RenderView.h"

#if __has_feature(modules)
@import QuartzCore.CAMetalLayer;
@import CoreVideo.CVDisplayLink;
#else
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CVDisplayLink.h>
#endif

#include "VulkanTutorial2.hpp"

@interface RenderView()
{
    CVDisplayLinkRef myDisplayLink;
    unsigned int myFrameIndex;
}
- (CVReturn)getFrameForTime:(const CVTimeStamp*)outputTime;
@end

static CVReturn displayLinkCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* now, const CVTimeStamp* outputTime, CVOptionFlags flagsIn, CVOptionFlags* flagsOut, void* displayLinkContext)
{
    return [(__bridge RenderView*)displayLinkContext getFrameForTime:outputTime];
}

@implementation RenderView

- (id)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    //self.bounds = frameRect;
    
    self.device = MTLCreateSystemDefaultDevice();
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    self.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    self.sampleCount = 1;//self.renderer.multisamples;
    self.delegate = (id <MTKViewDelegate>)self;
    
    // Set paused and only trigger redraw when needs display is set.
    self.paused = YES;
    self.enableSetNeedsDisplay = YES;
    
    self.wantsLayer = YES;
    
    const float backingScaleFactor = [NSScreen mainScreen].backingScaleFactor;
    CGSize size = CGSizeMake(self.bounds.size.width * backingScaleFactor, self.bounds.size.height * backingScaleFactor);
    
    if (![self.layer isKindOfClass:[CAMetalLayer class]])
        [self setLayer:[CAMetalLayer layer]];
    
    CAMetalLayer* renderLayer = [CAMetalLayer layer];
    renderLayer.device = self.device;
    renderLayer.pixelFormat = self.colorPixelFormat;
    renderLayer.framebufferOnly = YES;
    renderLayer.frame = self.bounds;
    renderLayer.drawableSize = size;
    
    myFrameIndex = 0;
    
    vktut2_create((__bridge void*)(self), (int)size.width, (int)size.height, backingScaleFactor);
    
    // Setup display link.
    CVDisplayLinkCreateWithActiveCGDisplays(&myDisplayLink);
    CVReturn error = CVDisplayLinkSetOutputCallback(myDisplayLink, &displayLinkCallback, (__bridge void*)self);
    NSAssert((kCVReturnSuccess == error), @"Setting Display Link callback error %d", error);
    error = CVDisplayLinkStart(myDisplayLink);
    NSAssert((kCVReturnSuccess == error), @"Creating Display Link error %d", error);
    
    return self;
}

- (void)dealloc
{
    CVDisplayLinkStop(myDisplayLink);
    
    vktut2_destroy();
}

- (CVReturn)getFrameForTime:(const CVTimeStamp*)outputTime
{
    if (true) //if view needs redraw
    {
        // Need to dispatch to main thread as CVDisplayLink uses it's own thread.
        dispatch_async(dispatch_get_main_queue(), ^{
            [self setNeedsDisplay:YES];
        });
    }
    return kCVReturnSuccess;
}

- (void)drawInMTKView:(nonnull MTKView*)view
{
    vktut2_drawframe(self->myFrameIndex++);
}

@end
