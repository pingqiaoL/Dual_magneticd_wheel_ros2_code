#pragma once

#include "video_streamer_transmission/VideoTypes.hpp"

#include <memory>

namespace video_streamer_transmission
{
class VideoStream
{
  public:
    explicit VideoStream(VideoConfig config);
    ~VideoStream();
    VideoStream(const VideoStream &) = delete;
    VideoStream &operator=(const VideoStream &) = delete;
    bool start();
    void stop();
    void submit_frame(VideoFrame frame);
    VideoStatus status() const;

  private:
    struct Implementation;
    std::unique_ptr<Implementation> _implementation;
};
} // namespace video_streamer_transmission
