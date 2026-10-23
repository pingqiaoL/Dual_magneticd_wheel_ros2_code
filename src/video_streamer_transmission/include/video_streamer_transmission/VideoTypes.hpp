#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace video_streamer_transmission
{
struct VideoConfig
{
    int width = 1920;
    int height = 1080;
    int frame_rate = 30;
    int bitrate_kbps = 6000;
    int keyframe_interval_frames = 10;
    int rtsp_port = 8554;
    std::string stream_path = "/live";
    int frame_timeout_ms = 2000;
    int max_frame_age_ms = 150;
};

// 像素内存由 shared_ptr owner 保持有效，普通 C++ 层不依赖 ROS 消息类型。
struct VideoFrame
{
    const std::uint8_t *pixels = nullptr;
    std::size_t size_bytes = 0;
    std::shared_ptr<const void> owner;
};

struct VideoStatus
{
    std::string state = "STOPPED";
    std::string last_error;
    std::uint64_t received_frames = 0;
    std::uint64_t submitted_frames = 0;
    std::uint64_t encoded_frames = 0;
    std::uint64_t dropped_frames = 0;
    std::uint32_t client_count = 0;
};
} // namespace video_streamer_transmission
