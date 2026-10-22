#pragma once

#include "telemetry_bridge/TelemetryDriver.hpp"

#include <rclcpp/rclcpp.hpp>
#include <robot_interfaces/msg/thickness_reading.hpp>

#include <memory>

namespace telemetry_bridge
{

class TelemetryBridgeNode final : public rclcpp::Node
{
  public:
    TelemetryBridgeNode();
    ~TelemetryBridgeNode() override;

  private:
    static constexpr std::int64_t MAX_THICKNESS_AGE_MS = 1000;

    void send_thickness(const robot_interfaces::msg::ThicknessReading &thick_data);
    void report_stm_link();
    void report_status();

    std::unique_ptr<TelemetryDriver> _driver;
    TelemetryStatus _last_status;
    rclcpp::Subscription<robot_interfaces::msg::ThicknessReading>::SharedPtr
        _thickness_subscription;
    rclcpp::TimerBase::SharedPtr _stm_link_timer;
    rclcpp::TimerBase::SharedPtr _status_timer;
};

} // namespace telemetry_bridge
