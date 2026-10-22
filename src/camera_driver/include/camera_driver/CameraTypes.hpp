#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace camera_driver
{
struct CameraConfig
{
    std::string source_mode = "camera";
    std::string device_path = "/dev/video0";
    std::string input_format = "MJPEG";
    int width = 1280;
    int height = 720;
    int frame_rate = 30;
    int frame_timeout_ms = 2000;
    int reconnect_interval_ms = 1000;
};

struct CameraFrame
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

struct CameraStatus
{
    std::string state = "STOPPED";
    std::string last_error;
    std::uint64_t captured_frames = 0;
    std::uint64_t reconnects = 0;
};
} // namespace camera_driver
