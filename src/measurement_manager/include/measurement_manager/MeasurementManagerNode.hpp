#pragma once

#include <rclcpp/rclcpp.hpp>
#include <robot_interfaces/msg/thickness_reading.hpp>
#include <robot_interfaces/msg/thickness_status.hpp>
#include <array>
#include <cstdint>
#include <string>

// 当前阶段只订阅并打印，不进行滤波、门控、记录或再次转发。
class MeasurementManagerNode final : public rclcpp::Node
{
public:
    MeasurementManagerNode();

private:
    void onReading(const robot_interfaces::msg::ThicknessReading &reading);
    void onStatus(const robot_interfaces::msg::ThicknessStatus &status);
    static std::string serialText(const std::array<std::uint8_t, 12> &serial);
    static const char *unitName(std::uint8_t unit);
    static const char *statusName(const robot_interfaces::msg::ThicknessStatus &status);

    bool _haveStatus{false};
    robot_interfaces::msg::ThicknessStatus _lastStatus;
    rclcpp::Subscription<robot_interfaces::msg::ThicknessReading>::SharedPtr _readingSubscription;
    rclcpp::Subscription<robot_interfaces::msg::ThicknessStatus>::SharedPtr _statusSubscription;
};
