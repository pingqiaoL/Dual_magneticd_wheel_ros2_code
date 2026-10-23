#include "camera_driver/CameraCaptureNode.hpp"

#include <rclcpp_components/register_node_macro.hpp>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>

namespace camera_driver
{
CameraCaptureNode::CameraCaptureNode(const rclcpp::NodeOptions &options)
    : Node("camera_capture_node", options)
{
    rcl_interfaces::msg::ParameterDescriptor descriptor;
    descriptor.read_only = true;
    descriptor.description = "Startup configuration; restart to change";
    CameraConfig config;
    config.source_mode = declare_parameter("source_mode", config.source_mode, descriptor);
    config.device_path = declare_parameter("device_path", config.device_path, descriptor);
    config.input_format = declare_parameter("input_format", config.input_format, descriptor);
    config.width = declare_parameter("width", config.width, descriptor);
    config.height = declare_parameter("height", config.height, descriptor);
    config.frame_rate = declare_parameter("frame_rate", config.frame_rate, descriptor);
    config.frame_timeout_ms =
        declare_parameter("frame_timeout_ms", config.frame_timeout_ms, descriptor);
    config.reconnect_interval_ms =
        declare_parameter("reconnect_interval_ms", config.reconnect_interval_ms, descriptor);
    _frame_id = declare_parameter<std::string>("frame_id", "camera_optical_frame", descriptor);
    _image_publisher = create_publisher<sensor_msgs::msg::Image>(
        "/camera/image_yuv", rclcpp::SensorDataQoS().keep_last(1));
    _info_publisher = create_publisher<sensor_msgs::msg::CameraInfo>(
        "/camera/camera_info", rclcpp::SensorDataQoS().keep_last(1));
    _status_publisher =
        create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/camera/status", 1);
    _device = std::make_unique<CameraDevice>(config);
    _status_timer = create_wall_timer(std::chrono::seconds(1), [this] { publish_status(); });
    _device->start(
        [this](std::unique_ptr<CameraFrame> frame)
        {
            publish_frame(std::move(frame));
        }); //驱动以后拿到一帧图像，就调用这个函数，把图像交给节点发布
    RCLCPP_INFO(get_logger(), "Camera source=%s device=%s format=%s %dx%d@%d; I420 output",
                config.source_mode.c_str(), config.device_path.c_str(), config.input_format.c_str(),
                config.width, config.height, config.frame_rate);
}

CameraCaptureNode::~CameraCaptureNode()
{
    _device->stop();
}

void CameraCaptureNode::publish_frame(std::unique_ptr<CameraFrame> frame)
{
    auto image = std::make_unique<sensor_msgs::msg::Image>();
    // 此时间为应用取到完整图像的 ROS 时间，不冒充硬件曝光时间。
    image->header.stamp = now();
    image->header.frame_id = _frame_id;
    image->width = frame->width;
    image->height = frame->height;
    image->encoding = "yuv420";
    image->step = frame->width;
    image->is_bigendian = false;
    image->data = std::move(frame->pixels);
    sensor_msgs::msg::CameraInfo info;
    info.header = image->header;
    info.width = image->width;
    info.height = image->height;
    // 未标定：K[0] 保持为 0，不能将单位矩阵伪装成真实相机内参。
    _info_publisher->publish(info);
    _image_publisher->publish(std::move(image));
}

void CameraCaptureNode::publish_status()
{
    const auto status = _device->status();
    diagnostic_msgs::msg::DiagnosticArray message;
    message.header.stamp = now();
    diagnostic_msgs::msg::DiagnosticStatus entry;
    entry.name = "camera_capture";
    entry.hardware_id = "usb_camera";
    entry.level = status.state == "CAPTURING" ? entry.OK : entry.WARN;
    entry.message = status.state;
    for (const auto &item : {std::pair<std::string, std::string>{
                                 "captured_frames", std::to_string(status.captured_frames)},
                             {"reconnects", std::to_string(status.reconnects)},
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
} // namespace camera_driver
RCLCPP_COMPONENTS_REGISTER_NODE(camera_driver::CameraCaptureNode)
