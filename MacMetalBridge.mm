#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>

CAMetalLayer* setupMetalLayerForView(void* nsviewPtr)
{
    NSView* view = (__bridge NSView*)nsviewPtr;

    view.wantsLayer = YES;

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.device = MTLCreateSystemDefaultDevice();
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.contentsScale = view.window.backingScaleFactor;

    // IMPORTANT: replace Qt’s layer with our CAMetalLayer
    view.layer = metalLayer;

    return metalLayer;
}
#endif

// void setupMetalLayerForView(void* nsviewPtr)
// {
//     NSView* view = (__bridge NSView*)nsviewPtr;

//     if (![view isKindOfClass:[NSView class]])
//     {
//         NSLog(@"[CRITICAL MVK ERROR] Provided pointer is not an NSView! It is a %@.", NSStringFromClass([view class]));
//         return;
//     }

//     // Define the setup logic in a block
//     void (^setupBlock)(void) = ^{
//         if (view == nil) {
//             NSLog(@"Error: NSView pointer is null.");
//             return;
//         }

//         // if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
//             // return; // Already set up
//         // }
//         if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
//           view.wantsLayer = YES;
//           view.layer = [CAMetalLayer layer];
//         }

//         CAMetalLayer* metalLayer = [CAMetalLayer layer];
//         //CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;
//         metalLayer.device = MTLCreateSystemDefaultDevice();

//         if (metalLayer.device == nil)
//         {
//           NSLog(@"[CRITICAL MVK ERROR] Failed to create default Metal device!");
//           // You might want a mechanism to signal this error back to C++
//           return;
//         }

//         metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;

//         NSRect bounds = [view bounds];
//         if (bounds.size.width <= 0 || bounds.size.height <= 0)
//         {
//           NSLog(@"[CRITICAL MVK ERROR] NSView bounds are invalid: width=%f, height=%f. Cannot create Metal Layer.", bounds.size.width, bounds.size.height);
//           // You might consider throwing an exception or returning an error code here
//           // to signal back to the C++ side that surface creation should be aborted/retried.
//           return;
//         }
//         metalLayer.frame = bounds;
//         metalLayer.contentsScale = [view.window backingScaleFactor];

//         view.wantsLayer = YES;
//         view.layer = metalLayer;

//         NSLog(@"[DEBUG MVK] NSView Layer Class: %@", NSStringFromClass([view.layer class]));
//         NSLog(@"[DEBUG MVK] NSView wantsLayer: %d", [view wantsLayer]);
//         NSLog(@"[DEBUG MVK] CAMetalLayer device: %@", metalLayer.device);
//         NSLog(@"[DEBUG MVK] CAMetalLayer pixelFormat: %lu", (unsigned long)metalLayer.pixelFormat);
//         NSLog(@"[DEBUG MVK] CAMetalLayer frame size: %f, %f", metalLayer.frame.size.width, metalLayer.frame.size.height);
//     };

//     // Check if we are currently on the main thread
//     if ([NSThread isMainThread]) {
//         setupBlock();
//     } else {
//         // If not on the main thread, run synchronously on the main thread
//         dispatch_sync(dispatch_get_main_queue(), setupBlock);
//     }
// }
