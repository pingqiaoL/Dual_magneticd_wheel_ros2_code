#pragma once

#include "thickness_usb_driver/ThicknessDevice.hpp"

#include <rclcpp/rclcpp.hpp>
#include <robot_interfaces/msg/thickness_reading.hpp>
#include <robot_interfaces/msg/thickness_status.hpp>

#include <cstdint>
#include <memory>
#include <vector>

/** ROS 2 应用适配层：参数、调度、普通 C++ 数据与 ROS 消息之间的转换。 */
class ThicknessNode final : public rclcpp::Node
{
  public:
    ThicknessNode();

  private:
    static constexpr int DEVICE_PROCESSING_PERIOD_MS = 10;
    static constexpr int STATUS_PUBLISH_PERIOD_MS = 500;

    void process_device_io();
    void publish_measurement(const DecodedMeasurement &measurement);
    void publish_status();

    std::unique_ptr<ThicknessDevice> _device;
    std::vector<DecodedMeasurement> _measurements;
    std::uint64_t _measurement_sequence{0};

    rclcpp::Publisher<robot_interfaces::msg::ThicknessReading>::SharedPtr _reading_publisher;
    rclcpp::Publisher<robot_interfaces::msg::ThicknessStatus>::SharedPtr _status_publisher;
    rclcpp::TimerBase::SharedPtr _device_processing_timer;
    rclcpp::TimerBase::SharedPtr _status_publish_timer;
};
