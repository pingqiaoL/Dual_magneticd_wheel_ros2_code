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
    void on_reading(const robot_interfaces::msg::ThicknessReading &reading);
    void on_status(const robot_interfaces::msg::ThicknessStatus &status);
    static std::string serial_number_to_text(const std::array<std::uint8_t, 12> &serial_number);
    static const char *unit_name(std::uint8_t unit);
    static const char *status_description(const robot_interfaces::msg::ThicknessStatus &status);

    bool _has_status{false};
    robot_interfaces::msg::ThicknessStatus _last_status;
    rclcpp::Subscription<robot_interfaces::msg::ThicknessReading>::SharedPtr _reading_subscription;
    rclcpp::Subscription<robot_interfaces::msg::ThicknessStatus>::SharedPtr _status_subscription;
};
