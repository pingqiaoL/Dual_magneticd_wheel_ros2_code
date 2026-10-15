#include "measurement_manager/MeasurementManagerNode.hpp"
#include <cinttypes>
#include <cmath>
#include <limits>

MeasurementManagerNode::MeasurementManagerNode() : Node("measurement_manager_node")
{
    _readingSubscription = create_subscription<robot_interfaces::msg::ThicknessReading>(
        "/thickness/reading", rclcpp::QoS(64).reliable().durability_volatile(),
        [this](const robot_interfaces::msg::ThicknessReading &reading) { onReading(reading); });
    _statusSubscription = create_subscription<robot_interfaces::msg::ThicknessStatus>(
        "/thickness/status", rclcpp::QoS(1).reliable().durability_volatile(),
        [this](const robot_interfaces::msg::ThicknessStatus &status) { onStatus(status); });
    RCLCPP_INFO(get_logger(), "测厚订阅节点已启动，等待采集节点发布数据；Ctrl+C退出。");
}

std::string MeasurementManagerNode::serialText(const std::array<std::uint8_t, 12> &serial)
{
    std::string text;
    bool present = false;
    for (const auto byte : serial)
    {
        present = present || byte != 0;
        text += byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '?';
    }
    return present ? text : "unknown";
}

const char *MeasurementManagerNode::unitName(std::uint8_t unit)
{
    return unit == robot_interfaces::msg::ThicknessReading::UNIT_UM ? "um" : "unknown";
}

const char *MeasurementManagerNode::statusName(const robot_interfaces::msg::ThicknessStatus &status)
{
    if (!status.port_open)
    {
        return "USB未打开，等待设备连接";
    }
    if (!status.communication_ready)
    {
        return status.connection_state == status.STALE ? "仪器应答超时，正在重试" : "USB已打开，等待仪器应答";
    }
    if (status.last_measurement_age_ms == std::numeric_limits<std::uint32_t>::max())
    {
        return "通信正常，等待测量上报";
    }
    return "通信正常，已收到测量上报";
}

void MeasurementManagerNode::onReading(const robot_interfaces::msg::ThicknessReading &reading)
{
    // 每条消息直接在订阅回调打印；显示原始浮点值，不做滤波或单位猜测。
    const auto serial = serialText(reading.device_sn);
    const bool finite = reading.finite && std::isfinite(reading.value);
    RCLCPP_INFO(get_logger(),
        "测量: SN=%s seq=%" PRIu64 " value=%.9g unit=%s material_raw=%u finite=%s",
        serial.c_str(), reading.sequence, static_cast<double>(reading.value), unitName(reading.unit),
        static_cast<unsigned int>(reading.material_raw), finite ? "true" : "false");
}

void MeasurementManagerNode::onStatus(const robot_interfaces::msg::ThicknessStatus &status)
{
    const auto never = std::numeric_limits<std::uint32_t>::max();
    const bool changed = !_haveStatus || status.connection_state != _lastStatus.connection_state ||
        status.port_open != _lastStatus.port_open || status.communication_ready != _lastStatus.communication_ready ||
        status.device_sn != _lastStatus.device_sn || status.last_error != _lastStatus.last_error ||
        (status.last_measurement_age_ms == never) != (_lastStatus.last_measurement_age_ms == never);

    if (changed)
    {
        const auto serial = serialText(status.device_sn);
        RCLCPP_INFO(get_logger(), "仪器状态: %s | SN=%s valid_frames=%" PRIu64 " crc_errors=%" PRIu64
            " io_errors=%" PRIu64 " reason=%s", statusName(status), serial.c_str(), status.valid_frames,
            status.crc_errors, status.io_errors, status.last_error.empty() ? "-" : status.last_error.c_str());
    }
    if (status.crc_errors != (_haveStatus ? _lastStatus.crc_errors : 0))
    {
        RCLCPP_WARN(get_logger(), "收到CRC错误帧，已丢弃；crc_errors=%" PRIu64, status.crc_errors);
    }
    _lastStatus = status;
    _haveStatus = true;
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    try
    {
        rclcpp::spin(std::make_shared<MeasurementManagerNode>());
    }
    catch (const std::exception &error)
    {
        RCLCPP_FATAL(rclcpp::get_logger("measurement_manager_node"), "%s", error.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
