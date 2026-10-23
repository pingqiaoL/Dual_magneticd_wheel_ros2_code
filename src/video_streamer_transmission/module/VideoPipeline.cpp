#include "video_streamer_transmission/VideoPipeline.hpp"

#include <sstream>
#include <stdexcept>

namespace video_streamer_transmission
{
void VideoPipeline::validate_config(const VideoConfig &config)
{
    if (config.width < 16 || config.width > 4096 || config.width % 2 != 0 || config.height < 16 ||
        config.height > 2160 || config.height % 2 != 0 || config.frame_rate < 1 ||
        config.frame_rate > 120 || config.bitrate_kbps < 100 || config.bitrate_kbps > 50000 ||
        config.keyframe_interval_frames < 1 || config.keyframe_interval_frames > 600 ||
        config.rtsp_port < 1024 || config.rtsp_port > 65535 || config.stream_path.size() < 2 ||
        config.stream_path[0] != '/' ||
        config.stream_path.find_first_not_of(
            "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos ||
        config.frame_timeout_ms < 100 || config.max_frame_age_ms < 1)
    {
        throw std::invalid_argument("invalid video configuration");
    }
}

std::string VideoPipeline::build_description(const VideoConfig &config)
{
    validate_config(config);
    std::ostringstream description;
    description << "( appsrc name=frames is-live=true format=time block=false "
                   "max-buffers=1 max-bytes=0 max-time=0 leaky-type=downstream "
                   "caps=video/x-raw,format=I420,width="
                << config.width << ",height=" << config.height << ",framerate=" << config.frame_rate
                << "/1 ! "
                   "queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
                   "x264enc tune=zerolatency speed-preset=ultrafast threads=2 sliced-threads=true "
                   "bframes=0 "
                   "bitrate="
                << config.bitrate_kbps << " key-int-max=" << config.keyframe_interval_frames
                << " ! video/x-h264,profile=baseline ! h264parse name=encoded ! "
                   "video/x-h264,stream-format=byte-stream,alignment=au ! "
                   "rtph264pay name=pay0 pt=96 config-interval=-1 )";
    return description.str();
}
} // namespace video_streamer_transmission
