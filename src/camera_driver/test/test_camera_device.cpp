#include "camera_driver/CameraDevice.hpp"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

int main()
{
    using namespace camera_driver;
    CameraConfig config;
    config.source_mode = "test";
    config.width = 320;
    config.height = 240;
    std::atomic<int> frames{0};
    CameraDevice device(config);
    device.start(
        [&frames](std::unique_ptr<CameraFrame> frame)
        {
            if (frame->width != 320 || frame->height != 240 ||
                frame->pixels.size() != 320 * 240 * 3)
            {
                throw std::runtime_error("invalid captured frame layout");
            }
            ++frames;
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    device.stop();
    if (frames < 3 || device.status().state != "STOPPED")
        throw std::runtime_error("capture/stop failed");
    const int stopped_frames = frames;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (frames != stopped_frames)
        throw std::runtime_error("frames after stop");
    device.start([&frames](std::unique_ptr<CameraFrame>) { ++frames; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    device.stop();
    if (frames <= stopped_frames)
        throw std::runtime_error("capture restart failed");
    config.source_mode = "camera";
    config.device_path = "/dev/robot_nonexistent_camera";
    CameraDevice absent(config);
    absent.start([](std::unique_ptr<CameraFrame>)
                 { throw std::runtime_error("unexpected frame"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (absent.status().reconnects < 1 || absent.status().last_error.empty())
        throw std::runtime_error("missing device not reported");
    absent.stop();
    config.width = 0;
    try
    {
        CameraDevice invalid(config);
    }
    catch (const std::invalid_argument &)
    {
        return 0;
    }
    throw std::runtime_error("invalid config accepted");
}
