#import "RenderView.h"

#include "VulkanTutorial2.hpp"

@interface RenderView()
{
    CVDisplayLinkRef displayLink;
    CAMetalLayer* renderLayer;
    unsigned int frameIndex;
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
    [self setBounds: frameRect];
    
    self->frameIndex = 0;
    
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
    
    // Setup display link.
    CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
    CVReturn error = CVDisplayLinkSetOutputCallback(displayLink, &displayLinkCallback, (__bridge void*)self);
    NSAssert((kCVReturnSuccess == error), @"Setting Display Link callback error %d", error);
    error = CVDisplayLinkStart(displayLink);
    NSAssert((kCVReturnSuccess == error), @"Creating Display Link error %d", error);
    
    const float backingScaleFactor = [NSScreen mainScreen].backingScaleFactor;
    
    renderLayer = [CAMetalLayer layer];
    renderLayer.device = self.device;
    renderLayer.pixelFormat = self.colorPixelFormat;
    renderLayer.framebufferOnly = YES;
    renderLayer.frame = self.bounds;
    renderLayer.drawableSize = CGSizeMake(self.bounds.size.width * backingScaleFactor, self.bounds.size.height * backingScaleFactor);
    
    [self setLayer: renderLayer];
    
    vktut2_create((__bridge void *)(self), (int)self.bounds.size.width, (int)self.bounds.size.height);
    
    return self;
}

- (void)dealloc
{
    vktut2_destroy();
    
    CVDisplayLinkStop(displayLink);
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

- (void)drawInMTKView:(nonnull MTKView *)view
{
    vktut2_drawframe(self->frameIndex++);
}

@end
