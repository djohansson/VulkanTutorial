#pragma once

#if __has_feature(modules)
@import MetalKit;
@import QuartzCore.CAMetalLayer;
@import CoreVideo.CVDisplayLink;
#else
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CVDisplayLink.h>
#endif

@interface RenderView : MTKView

@end
