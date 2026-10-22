#include "video_streamer_transmission/VideoStreamerNode.hpp"

#include <rclcpp_components/register_node_macro.hpp>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>

namespace video_streamer_transmission
{
VideoStreamerNode::VideoStreamerNode(const rclcpp::NodeOptions &options)
    : Node("video_streamer_node", options)
{
    rcl_interfaces::msg::ParameterDescriptor descriptor;
    descriptor.read_only = true;
    descriptor.description = "Startup configuration; restart to change";
    _config.width = declare_parameter("width", _config.width, descriptor);
    _config.height = declare_parameter("height", _config.height, descriptor);
    _config.frame_rate = declare_parameter("frame_rate", _config.frame_rate, descriptor);
    _config.bitrate_kbps = declare_parameter("bitrate_kbps", _config.bitrate_kbps, descriptor);
    _config.keyframe_interval_frames =
        declare_parameter("keyframe_interval_frames", _config.keyframe_interval_frames, descriptor);
    _config.rtsp_port = declare_parameter("rtsp_port", _config.rtsp_port, descriptor);
    _config.stream_path = declare_parameter("stream_path", _config.stream_path, descriptor);
    _config.frame_timeout_ms =
        declare_parameter("frame_timeout_ms", _config.frame_timeout_ms, descriptor);
    _config.max_frame_age_ms =
        declare_parameter("max_frame_age_ms", _config.max_frame_age_ms, descriptor);
    _stream = std::make_unique<VideoStream>(_config);
    _status_publisher = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/video/status", 1);
    _image_subscription = create_subscription<sensor_msgs::msg::Image>(
        "/camera/image_raw", rclcpp::SensorDataQoS().keep_last(1),
        [this](sensor_msgs::msg::Image::ConstSharedPtr image) { on_image(std::move(image)); });
    _enable_service = create_service<std_srvs::srv::SetBool>(
        "/video/set_enabled",
        [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
               std::shared_ptr<std_srvs::srv::SetBool::Response> response)
        {
            if (request->data)
            {
                response->success = _stream->start();
            }
            else
            {
                _stream->stop();
                response->success = true;
            }
            response->message = _stream->status().state;
        });
    _status_timer = create_wall_timer(std::chrono::seconds(1), [this] { publish_status(); });
    if (!_stream->start())
    {
        throw std::runtime_error("RTSP startup failed: " + _stream->status().last_error);
    }
    RCLCPP_INFO(get_logger(),
                "H264 RTSP :%d%s %dx%d@%d bitrate=%d kbps; video starts on client request",
                _config.rtsp_port, _config.stream_path.c_str(), _config.width, _config.height,
                _config.frame_rate, _config.bitrate_kbps);
}

VideoStreamerNode::~VideoStreamerNode()
{
    _stream->stop();
}

void VideoStreamerNode::on_image(sensor_msgs::msg::Image::ConstSharedPtr image)
{
    if (image->encoding != "bgr8" || image->width != static_cast<std::uint32_t>(_config.width) ||
        image->height != static_cast<std::uint32_t>(_config.height) ||
        image->step != image->width * 3 ||
        image->data.size() != static_cast<std::size_t>(image->step) * image->height)
    {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                             "Rejected incompatible image dimensions/stride/encoding");
        return;
    }
    const auto age_ns = (now() - rclcpp::Time(image->header.stamp)).nanoseconds();
    if (age_ns < 0 || age_ns > static_cast<std::int64_t>(_config.max_frame_age_ms) * 1000000)
    {
        return;
    }
    VideoFrame frame;
    frame.pixels = image->data.data();
    frame.size_bytes = image->data.size();
    frame.owner = std::move(image);
    _stream->submit_frame(std::move(frame));
}

void VideoStreamerNode::publish_status()
{
    const auto status = _stream->status();
    diagnostic_msgs::msg::DiagnosticArray message;
    message.header.stamp = now();
    diagnostic_msgs::msg::DiagnosticStatus entry;
    entry.name = "video_stream";
    entry.hardware_id = "h264_rtsp";
    entry.level = status.state == "ERROR"           ? entry.ERROR
                  : status.state == "WAITING_FRAME" ? entry.WARN
                                                    : entry.OK;
    entry.message = status.state;
    for (const auto &item : {std::pair<std::string, std::string>{
                                 "received_frames", std::to_string(status.received_frames)},
                             {"submitted_frames", std::to_string(status.submitted_frames)},
                             {"encoded_frames", std::to_string(status.encoded_frames)},
                             {"dropped_frames", std::to_string(status.dropped_frames)},
                             {"client_count", std::to_string(status.client_count)},
                             {"last_error", status.last_error}})
    {
        diagnostic_msgs::msg::KeyValue value;
        value.key = item.first;
        value.value = item.second;
        entry.values.push_back(value);
    }
    message.status.push_back(std::move(entry));
    _status_publisher->publish(message);
}
} // namespace video_streamer_transmission
RCLCPP_COMPONENTS_REGISTER_NODE(video_streamer_transmission::VideoStreamerNode)
