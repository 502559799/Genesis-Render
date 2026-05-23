/*
 * Camera Capture Interface for macOS
 * Author: Genesis Render Team
 */

#ifndef __GENESIS_CAMERA_CAPTURE_H__
#define __GENESIS_CAMERA_CAPTURE_H__

#include <cstdint>
#include <functional>

namespace Genesis
{

struct CameraFrame
{
    uint8_t* data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bytesPerRow = 0;
    uint64_t timestamp = 0;
};

using FrameCallback = std::function<void(const CameraFrame& frame)>;

// Forward declaration
struct NativeCameraImpl;

class CameraCapture
{
public:
    CameraCapture();
    ~CameraCapture();

    bool init(uint32_t width = 1280, uint32_t height = 720, float fps = 30.0f);
    void start();
    void stop();
    void setFrameCallback(FrameCallback callback);

    bool isRunning() const { return running_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }

private:
    NativeCameraImpl* native_impl_ = nullptr;
    FrameCallback frame_callback_;
    bool running_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace Genesis

#endif // __GENESIS_CAMERA_CAPTURE_H__
