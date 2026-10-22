#pragma once

#include "camera_driver/CameraTypes.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace camera_driver
{
class CameraDevice
{
  public:
    using FrameCallback = std::function<void(std::unique_ptr<CameraFrame>)>;
    explicit CameraDevice(CameraConfig config);
    ~CameraDevice();
    CameraDevice(const CameraDevice &) = delete;
    CameraDevice &operator=(const CameraDevice &) = delete;
    void start(FrameCallback callback);
    void stop();
    CameraStatus status() const;
    static void validate_config(const CameraConfig &config);

  private:
    void capture_loop();
    void set_state(const std::string &state, const std::string &error = "");
    CameraConfig _config;
    FrameCallback _callback;
    std::atomic<bool> _running{false};
    std::thread _worker;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    CameraStatus _status;
};
} // namespace camera_driver
