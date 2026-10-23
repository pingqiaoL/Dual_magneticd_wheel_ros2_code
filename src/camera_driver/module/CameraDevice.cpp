#include "camera_driver/CameraDevice.hpp"

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace camera_driver
{
CameraDevice::CameraDevice(CameraConfig config) : _config(std::move(config))
{
    validate_config(_config);
    gst_init(nullptr, nullptr);
}

CameraDevice::~CameraDevice()
{
    stop();
}

void CameraDevice::validate_config(const CameraConfig &config)
{
    if ((config.source_mode != "camera" && config.source_mode != "test") ||
        (config.input_format != "MJPEG" && config.input_format != "YUYV" &&
         config.input_format != "H264") ||
        config.device_path.rfind("/dev/", 0) != 0 ||
        config.device_path.find('\0') != std::string::npos || config.width < 16 ||
        config.width > 4096 || config.height < 16 || config.height > 2160 ||
        config.width % 2 != 0 || config.height % 2 != 0 || config.frame_rate < 1 ||
        config.frame_rate > 120 || config.frame_timeout_ms < 100 ||
        config.reconnect_interval_ms < 100)
    {
        throw std::invalid_argument("invalid camera configuration");
    }
}

void CameraDevice::start(FrameCallback callback)
{
    if (_running || !callback)
    {
        throw std::logic_error("camera already started or empty frame callback");
    }
    _callback = std::move(callback);
    _running = true;
    _worker = std::thread(&CameraDevice::capture_loop, this);
}

void CameraDevice::stop()
{
    _running = false;
    _wake.notify_all();
    if (_worker.joinable())
    {
        _worker.join();
    }
    set_state("STOPPED");
}

void CameraDevice::set_state(const std::string &state, const std::string &error)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _status.state = state;
    _status.last_error = error;
}

CameraStatus CameraDevice::status() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _status;
}

void CameraDevice::capture_loop()
{
    while (_running)
    {
        set_state("OPENING");
        gchar *escaped_path = g_strescape(_config.device_path.c_str(), nullptr);
        std::ostringstream description;
        if (_config.source_mode == "test")
        {
            description << "videotestsrc is-live=true pattern=ball ! video/x-raw,width=";
        }
        else
        {
            description << "v4l2src device=\"" << escaped_path << "\" do-timestamp=true ! ";
            description << (_config.input_format == "MJPEG"  ? "image/jpeg,width="
                            : _config.input_format == "H264" ? "video/x-h264,width="
                                                             : "video/x-raw,format=YUY2,width=");
        }
        g_free(escaped_path);
        description << _config.width << ",height=" << _config.height
                    << ",framerate=" << _config.frame_rate << "/1 ! ";
        if (_config.source_mode == "camera" && _config.input_format == "MJPEG")
        {
            description << "jpegdec ! ";
        }
        if (_config.source_mode == "camera" && _config.input_format == "H264")
        {
            description << "h264parse ! avdec_h264 ! ";
        }
        description << "videoconvert ! video/x-raw,format=I420 ! "
                       "appsink name=frames max-buffers=1 drop=true sync=false";
        GError *error = nullptr;
        GstElement *pipeline = gst_parse_launch(description.str().c_str(), &error);
        std::string reason = error ? error->message : "";
        g_clear_error(&error);
        GstElement *sink =
            pipeline && reason.empty() ? gst_bin_get_by_name(GST_BIN(pipeline), "frames") : nullptr;
        GstBus *bus = pipeline ? gst_element_get_bus(pipeline) : nullptr;
        if (!sink || gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
            if (reason.empty())
            {
                reason = "camera pipeline could not enter PLAYING";
            }
        }
        else
        {
            auto last_frame_at = std::chrono::steady_clock::now();
            while (_running)
            {
                GstMessage *message = gst_bus_pop_filtered(
                    bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
                if (message)
                {
                    reason = "camera end of stream";
                    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
                    {
                        GError *stream_error = nullptr;
                        gst_message_parse_error(message, &stream_error, nullptr);
                        reason = stream_error ? stream_error->message : "camera stream error";
                        g_clear_error(&stream_error);
                    }
                    gst_message_unref(message);
                    break;
                }
                GstSample *sample =
                    gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 100 * GST_MSECOND);
                if (!sample)
                {
                    if (std::chrono::steady_clock::now() - last_frame_at >
                        std::chrono::milliseconds(_config.frame_timeout_ms))
                    {
                        reason = "camera frame timeout";
                        break;
                    }
                    continue;
                }
                GstVideoInfo info;
                gst_video_info_init(&info);
                GstVideoFrame video_frame;
                if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) ||
                    GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420 ||
                    !gst_video_frame_map(&video_frame, &info, gst_sample_get_buffer(sample),
                                         GST_MAP_READ))
                {
                    gst_sample_unref(sample);
                    reason = "invalid I420 camera sample";
                    break;
                }
                auto frame = std::make_unique<CameraFrame>();
                frame->width = GST_VIDEO_INFO_WIDTH(&info);
                frame->height = GST_VIDEO_INFO_HEIGHT(&info);
                const std::size_t y_size = static_cast<std::size_t>(frame->width) * frame->height;
                const std::size_t chroma_size = y_size / 4;
                frame->pixels.resize(y_size + 2 * chroma_size);
                const std::size_t plane_offsets[3]{0, y_size, y_size + chroma_size};
                const std::size_t row_bytes[3]{frame->width, frame->width / 2, frame->width / 2};
                const std::size_t rows[3]{frame->height, frame->height / 2, frame->height / 2};
                for (std::size_t plane = 0; plane < 3; ++plane)
                {
                    const auto *pixels = static_cast<const std::uint8_t *>(
                        GST_VIDEO_FRAME_PLANE_DATA(&video_frame, plane));
                    const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&video_frame, plane);
                    for (std::size_t row = 0; row < rows[plane]; ++row)
                    {
                        std::memcpy(frame->pixels.data() + plane_offsets[plane] +
                                        row * row_bytes[plane],
                                    pixels + row * stride, row_bytes[plane]);
                    }
                }
                gst_video_frame_unmap(&video_frame);
                gst_sample_unref(sample);
                last_frame_at = std::chrono::steady_clock::now();
                {
                    std::lock_guard<std::mutex> guard(_mutex);
                    _status.state = "CAPTURING";
                    _status.last_error.clear();
                    ++_status.captured_frames;
                }
                try
                {
                    _callback(std::move(frame));
                }
                catch (const std::exception &callback_error)
                {
                    reason = callback_error.what();
                    break;
                }
            }
        }
        if (pipeline)
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
        }
        if (sink)
            gst_object_unref(sink);
        if (bus)
            gst_object_unref(bus);
        if (pipeline)
            gst_object_unref(pipeline);
        if (!_running)
            break;
        set_state("RECONNECTING", reason);
        std::unique_lock<std::mutex> guard(_mutex);
        ++_status.reconnects;
        _wake.wait_for(guard, std::chrono::milliseconds(_config.reconnect_interval_ms),
                       [this] { return !_running; });
    }
}
} // namespace camera_driver
