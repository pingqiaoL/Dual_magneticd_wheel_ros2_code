#include "telemetry_bridge/TelemetryBridgeNode.hpp"

#include <rcl_interfaces/msg/parameter_descriptor.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace telemetry_bridge
{

TelemetryBridgeNode::TelemetryBridgeNode() : Node("telemetry_bridge_node")
{
    rcl_interfaces::msg::ParameterDescriptor descriptor;
    descriptor.read_only = true; //不允许启动后参数修改
    descriptor.description = "启动配置，修改后重启节点";
    TelemetryConfig config;
    config.device_path = declare_parameter<std::string>("device_path", config.device_path,
                                                        descriptor); //声明为ros2 parameter
    const auto baud_rate =
        declare_parameter<std::int64_t>("baud_rate", config.baud_rate, descriptor);
    if (baud_rate < 0 ||
        static_cast<std::uint64_t>(baud_rate) > std::numeric_limits<unsigned int>::max())
    {
        throw std::invalid_argument("telemetry baud_rate is outside unsigned int range");
    }
    config.baud_rate = static_cast<unsigned int>(baud_rate);
    _driver = std::make_unique<TelemetryDriver>(config);
    _driver->start(); //开启数传驱动
    _thickness_subscription = create_subscription<robot_interfaces::msg::ThicknessReading>(
        "/measurement/thickness", rclcpp::QoS(64).reliable().durability_volatile(),
        [this](const robot_interfaces::msg::ThicknessReading &thick_data)
        { send_thickness(thick_data); });
    _stm_link_timer = create_wall_timer(std::chrono::seconds(1), [this] { report_stm_link(); });
    _status_timer = create_wall_timer(std::chrono::seconds(1), [this] { report_status(); });
    RCLCPP_INFO(get_logger(), "MK32 数传节点: %s %u 8N1, MAVLink 2, 地址 1/191",
                config.device_path.c_str(), config.baud_rate);
}

TelemetryBridgeNode::~TelemetryBridgeNode()
{
    _driver->stop();
}

void TelemetryBridgeNode::send_thickness(const robot_interfaces::msg::ThicknessReading &thick_data)
{
    const auto age_ms = (now() - rclcpp::Time(thick_data.received_at)).nanoseconds() / 1000000;
    if (!thick_data.finite || !std::isfinite(thick_data.value) || age_ms < 0 ||
        age_ms > MAX_THICKNESS_AGE_MS)
    {
        return;
    }
    (void)_driver->send_named_float("THICKNESS", thick_data.value);
}

void TelemetryBridgeNode::report_stm_link()
{
    // 真实下位机状态尚未接入；业务状态由应用层提供，不能由串口打开状态推断。
    (void)_driver->send_named_int("STM_LINK", 0);
}

// 周期打印数传模块状态。
void TelemetryBridgeNode::report_status()
{
    const auto status = _driver->status();
    if (status.port_open != _last_status.port_open || status.io_errors != _last_status.io_errors ||
        status.written_telemetry != _last_status.written_telemetry ||
        status.discarded_telemetry != _last_status.discarded_telemetry)
    {
        RCLCPP_INFO(get_logger(),
                    "数传: port_open=%s written=%llu discarded=%llu queued=%zu errors=%llu %s",
                    status.port_open ? "true" : "false",
                    static_cast<unsigned long long>(status.written_telemetry),
                    static_cast<unsigned long long>(status.discarded_telemetry),
                    status.queued_frames, static_cast<unsigned long long>(status.io_errors),
                    status.last_error.c_str());
    }
    _last_status = status;
}

} // namespace telemetry_bridge

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    try
    {
        rclcpp::spin(std::make_shared<telemetry_bridge::TelemetryBridgeNode>());
    }
    catch (const std::exception &error) //
    {
        RCLCPP_FATAL(rclcpp::get_logger("telemetry_bridge_node"), "%s", error.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
