/*
 * Camera Capture Implementation using AVFoundation
 * Author: Genesis Render Team
 */

#include "camera_capture.h"
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

@interface CameraCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) Genesis::FrameCallback callback;
@property (nonatomic, assign) uint32_t width;
@property (nonatomic, assign) uint32_t height;
@end

@implementation CameraCaptureDelegate
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection
{
    if (!self.callback) return;

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);

    Genesis::CameraFrame frame;
    frame.data = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));
    frame.width = (uint32_t)CVPixelBufferGetWidth(pixelBuffer);
    frame.height = (uint32_t)CVPixelBufferGetHeight(pixelBuffer);
    frame.bytesPerRow = (uint32_t)CVPixelBufferGetBytesPerRow(pixelBuffer);

    CMTime timestamp = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    frame.timestamp = (uint64_t)(CMTimeGetSeconds(timestamp) * 1000000000.0);

    self.callback(frame);

    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
}
@end

namespace Genesis
{

struct NativeCameraImpl
{
    AVCaptureSession* session = nil;
    AVCaptureDeviceInput* input = nil;
    AVCaptureVideoDataOutput* output = nil;
    CameraCaptureDelegate* delegate = nil;
    dispatch_queue_t queue = nullptr;
};

CameraCapture::CameraCapture()
    : native_impl_(new NativeCameraImpl())
{
}

CameraCapture::~CameraCapture()
{
    stop();
    if (native_impl_->queue) {
        dispatch_release(native_impl_->queue);
    }
    delete native_impl_;
}

bool CameraCapture::init(uint32_t width, uint32_t height, float fps)
{
    width_ = width;
    height_ = height;

    // Request camera permission
    __block BOOL granted = NO;
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);

    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL authorized) {
        granted = authorized;
        dispatch_semaphore_signal(sema);
    }];

    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
    dispatch_release(sema);

    if (!granted) {
        printf("Camera permission denied\n");
        return false;
    }

    // Create capture session
    native_impl_->session = [[AVCaptureSession alloc] init];
    native_impl_->session.sessionPreset = AVCaptureSessionPreset1280x720;

    // Find default video device
    AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!device) {
        printf("No camera device found\n");
        return false;
    }

    NSError* error = nil;
    native_impl_->input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
    if (!native_impl_->input || error) {
        printf("Failed to create device input: %s\n", [[error localizedDescription] UTF8String]);
        return false;
    }

    if ([native_impl_->session canAddInput:native_impl_->input]) {
        [native_impl_->session addInput:native_impl_->input];
    } else {
        printf("Cannot add input to session\n");
        return false;
    }

    // Create video data output
    native_impl_->output = [[AVCaptureVideoDataOutput alloc] init];
    native_impl_->output.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey : @(width),
        (id)kCVPixelBufferHeightKey : @(height)
    };

    native_impl_->queue = dispatch_queue_create("camera_queue", DISPATCH_QUEUE_SERIAL);
    native_impl_->delegate = [[CameraCaptureDelegate alloc] init];
    native_impl_->delegate.width = width;
    native_impl_->delegate.height = height;

    [native_impl_->output setSampleBufferDelegate:native_impl_->delegate queue:native_impl_->queue];

    if ([native_impl_->session canAddOutput:native_impl_->output]) {
        [native_impl_->session addOutput:native_impl_->output];
    } else {
        printf("Cannot add output to session\n");
        return false;
    }

    return true;
}

void CameraCapture::start()
{
    if (native_impl_->session && !native_impl_->session.isRunning) {
        [native_impl_->session startRunning];
        running_ = true;
    }
}

void CameraCapture::stop()
{
    if (native_impl_->session && native_impl_->session.isRunning) {
        [native_impl_->session stopRunning];
        running_ = false;
    }
}

void CameraCapture::setFrameCallback(FrameCallback callback)
{
    frame_callback_ = callback;
    native_impl_->delegate.callback = callback;
}

} // namespace Genesis
