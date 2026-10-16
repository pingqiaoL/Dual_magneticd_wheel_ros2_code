#include "measurement_manager/MeasurementManagerNode.hpp"
#include <cinttypes>
#include <cmath>
#include <limits>

MeasurementManagerNode::MeasurementManagerNode() : Node("measurement_manager_node")
{
    _reading_subscription = create_subscription<robot_interfaces::msg::ThicknessReading>(
        "/thickness/reading", rclcpp::QoS(64).reliable().durability_volatile(),
        [this](const robot_interfaces::msg::ThicknessReading &reading) { on_reading(reading); });
    _status_subscription = create_subscription<robot_interfaces::msg::ThicknessStatus>(
        "/thickness/status", rclcpp::QoS(1).reliable().durability_volatile(),
        [this](const robot_interfaces::msg::ThicknessStatus &status) { on_status(status); });
    RCLCPP_INFO(get_logger(), "测厚订阅节点已启动，等待采集节点发布数据；Ctrl+C退出。");
}

std::string
MeasurementManagerNode::serial_number_to_text(const std::array<std::uint8_t, 12> &serial_number)
{
    std::string serial_number_text;
    bool has_serial_number = false;
    for (const auto byte : serial_number)
    {
        has_serial_number = has_serial_number || byte != 0;
        serial_number_text += byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '?';
    }
    return has_serial_number ? serial_number_text : "unknown";
}

const char *MeasurementManagerNode::unit_name(std::uint8_t unit)
{
    return unit == robot_interfaces::msg::ThicknessReading::UNIT_UM ? "um" : "unknown";
}

const char *
MeasurementManagerNode::status_description(const robot_interfaces::msg::ThicknessStatus &status)
{
    if (!status.port_open)
    {
        return "USB未打开，等待设备连接";
    }
    if (!status.communication_ready)
    {
        return status.connection_state == status.STALE ? "仪器应答超时，正在重试"
                                                       : "USB已打开，等待仪器应答";
    }
    if (status.last_measurement_age_ms == std::numeric_limits<std::uint32_t>::max())
    {
        return "通信正常，等待测量上报";
    }
    return "通信正常，已收到测量上报";
}

void MeasurementManagerNode::on_reading(const robot_interfaces::msg::ThicknessReading &reading)
{
    // 每条消息直接在订阅回调打印；显示原始浮点值，不做滤波或单位猜测。
    const auto serial_number_text = serial_number_to_text(reading.device_sn);
    const bool is_finite = reading.finite && std::isfinite(reading.value);
    RCLCPP_INFO(get_logger(),
                "测量: SN=%s seq=%" PRIu64 " value=%.9g unit=%s material_raw=%u finite=%s",
                serial_number_text.c_str(), reading.sequence, static_cast<double>(reading.value),
                unit_name(reading.unit), static_cast<unsigned int>(reading.material_raw),
                is_finite ? "true" : "false");
}

void MeasurementManagerNode::on_status(const robot_interfaces::msg::ThicknessStatus &status)
{
    constexpr std::uint32_t UNKNOWN_AGE_MS = std::numeric_limits<std::uint32_t>::max();
    const bool has_status_changed =
        !_has_status || status.connection_state != _last_status.connection_state ||
        status.port_open != _last_status.port_open ||
        status.communication_ready != _last_status.communication_ready ||
        status.device_sn != _last_status.device_sn ||
        status.last_error != _last_status.last_error ||
        (status.last_measurement_age_ms == UNKNOWN_AGE_MS) !=
            (_last_status.last_measurement_age_ms == UNKNOWN_AGE_MS);

    if (has_status_changed)
    {
        const auto serial_number_text = serial_number_to_text(status.device_sn);
        RCLCPP_INFO(get_logger(),
                    "仪器状态: %s | SN=%s valid_frames=%" PRIu64 " crc_errors=%" PRIu64
                    " io_errors=%" PRIu64 " reason=%s",
                    status_description(status), serial_number_text.c_str(), status.valid_frames,
                    status.crc_errors, status.io_errors,
                    status.last_error.empty() ? "-" : status.last_error.c_str());
    }
    if (status.crc_errors != (_has_status ? _last_status.crc_errors : 0))
    {
        RCLCPP_WARN(get_logger(), "收到CRC错误帧，已丢弃；crc_errors=%" PRIu64, status.crc_errors);
    }
    _last_status = status;
    _has_status = true;
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
