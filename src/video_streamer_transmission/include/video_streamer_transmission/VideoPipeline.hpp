#pragma once

#include "video_streamer_transmission/VideoTypes.hpp"

#include <string>

namespace video_streamer_transmission
{
class VideoPipeline
{
  public:
    static void validate_config(const VideoConfig &config);
    static std::string build_description(const VideoConfig &config);
};
} // namespace video_streamer_transmission
