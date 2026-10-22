#pragma once

#include <rclcpp/rclcpp.hpp>
#include <robot_interfaces/msg/thickness_reading.hpp>

// 业务启停与测厚数据转发；使用单线程 executor，不负责设备协议和数传串口。
class MeasurementManagerNode final : public rclcpp::Node
{
  public:
    MeasurementManagerNode();

    // 下位机状态订阅未启用；未来在本节点的订阅回调调用此入口。
    // true 持续发布后续新读数，false 停止发布，不停止仪器采集。
    void on_measurement_ready(bool is_ready);

  private:
    void update_data(const robot_interfaces::msg::ThicknessReading &thick_data);

    bool _is_measurement_enabled{false};
    rclcpp::Time _enabled_at{0, 0, RCL_ROS_TIME};
    rclcpp::Subscription<robot_interfaces::msg::ThicknessReading>::SharedPtr
        _thick_data_subscription;
    rclcpp::Publisher<robot_interfaces::msg::ThicknessReading>::SharedPtr _processed_data_publisher;
};
