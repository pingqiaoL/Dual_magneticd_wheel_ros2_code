#include "thickness_usb_driver/ThicknessNode.hpp"

#include <rcl_interfaces/msg/parameter_descriptor.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

ThicknessNode::ThicknessNode() : Node("thickness_node")
{
    rcl_interfaces::msg::ParameterDescriptor parameter_descriptor;
    parameter_descriptor.read_only = true;
    parameter_descriptor.description = "启动参数；修改后重新启动节点";

    ThicknessDeviceConfig device_config;
    device_config.device_path =
        declare_parameter<std::string>("device_path", device_config.device_path,
                                       parameter_descriptor);
    const auto baud_rate = declare_parameter<std::int64_t>(
        "baud_rate", device_config.baud_rate, parameter_descriptor);
    if (baud_rate < 0 ||
        static_cast<std::uint64_t>(baud_rate) > std::numeric_limits<unsigned int>::max())
    {
        throw std::invalid_argument("baud_rate is outside unsigned int range");
    }
    device_config.baud_rate = static_cast<unsigned int>(baud_rate);
    _device = std::make_unique<ThicknessDevice>(std::move(device_config));

    _reading_publisher = create_publisher<robot_interfaces::msg::ThicknessReading>(
        "/thickness/reading", rclcpp::QoS(64).reliable().durability_volatile());
    _status_publisher = create_publisher<robot_interfaces::msg::ThicknessStatus>(
        "/thickness/status", rclcpp::QoS(1).reliable().durability_volatile());
    _device_processing_timer = create_wall_timer(
        std::chrono::milliseconds(DEVICE_PROCESSING_PERIOD_MS),
        [this] { process_device_io(); });
    _status_publish_timer = create_wall_timer(
        std::chrono::milliseconds(STATUS_PUBLISH_PERIOD_MS), [this] { publish_status(); });

    RCLCPP_INFO(get_logger(), "测厚采集节点已启动: device_path=%s baud_rate=%u unit=unknown",
                _device->config().device_path.c_str(), _device->config().baud_rate);
}

void ThicknessNode::process_device_io()
{
    _device->process_io(_measurements);
    for (const auto &measurement : _measurements)
    {
        publish_measurement(measurement);
    }
}

void ThicknessNode::publish_measurement(const DecodedMeasurement &measurement)
{
    robot_interfaces::msg::ThicknessReading reading_message;
    reading_message.received_at = now();
    reading_message.sequence = ++_measurement_sequence;
    reading_message.device_sn = measurement.device_serial_number;
    reading_message.value = measurement.thickness_value;
    reading_message.unit = reading_message.UNIT_UNKNOWN;
    reading_message.material_raw = measurement.raw_material_code;
    reading_message.finite = std::isfinite(measurement.thickness_value);
    _reading_publisher->publish(reading_message);
}

void ThicknessNode::publish_status()
{
    const auto device_status = _device->status();
    robot_interfaces::msg::ThicknessStatus status_message;
    status_message.stamp = now();
    status_message.port_open = device_status.port_open;
    status_message.communication_ready = device_status.communication_ready;
    status_message.device_sn = device_status.device_serial_number;
    status_message.last_response_age_ms = device_status.last_response_age_ms;
    status_message.last_measurement_age_ms = device_status.last_measurement_age_ms;
    status_message.valid_frames = device_status.quality.valid_frames;
    status_message.crc_errors = device_status.quality.crc_errors;
    status_message.io_errors = device_status.io_error_count;
    status_message.last_error = device_status.last_error.substr(0, 160);

    switch (device_status.connection_state)
    {
        case ThicknessConnectionState::DISCONNECTED:
            status_message.connection_state = status_message.DISCONNECTED;
            break;
        case ThicknessConnectionState::CONNECTING:
            status_message.connection_state = status_message.CONNECTING;
            break;
        case ThicknessConnectionState::ONLINE:
            status_message.connection_state = status_message.ONLINE;
            break;
        case ThicknessConnectionState::STALE:
            status_message.connection_state = status_message.STALE;
            break;
    }

    _status_publisher->publish(status_message);
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    try
    {
        rclcpp::spin(std::make_shared<ThicknessNode>());
    }
    catch (const std::exception &error)
    {
        RCLCPP_FATAL(rclcpp::get_logger("thickness_node"), "%s", error.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
