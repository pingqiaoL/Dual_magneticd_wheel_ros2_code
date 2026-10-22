#include "measurement_manager/MeasurementManagerNode.hpp"

#include <cinttypes>
#include <cmath>
#include <exception>

MeasurementManagerNode::MeasurementManagerNode() : Node("measurement_manager_node")
{
    _processed_data_publisher = create_publisher<robot_interfaces::msg::ThicknessReading>(
        "/measurement/thickness", rclcpp::QoS(64).reliable().durability_volatile());
    // 当前默认开启。下位机状态话题尚未接入，后续回调调用同一个启停入口。
    on_measurement_ready(true); // 后续改为订阅 robot_control 节点信息来启用。
    _thick_data_subscription = create_subscription<robot_interfaces::msg::ThicknessReading>(
        "/thickness/reading", rclcpp::QoS(64).reliable().durability_volatile(),
        [this](const robot_interfaces::msg::ThicknessReading &thick_data)
        { update_data(thick_data); });
    RCLCPP_INFO(get_logger(), "测厚管理节点已启动，默认发布有效新读数；下位机状态订阅未启用。");
}
// 测量状态判断。
void MeasurementManagerNode::on_measurement_ready(bool is_ready)
{
    if (is_ready && !_is_measurement_enabled)
    {
        // 重复的就绪状态不刷新时间，避免持续状态上报阻止正常测量发布。
        _enabled_at = now();
    }
    _is_measurement_enabled = is_ready;
}
// 处理测厚数据。
void MeasurementManagerNode::update_data(const robot_interfaces::msg::ThicknessReading &thick_data)
{
    if (!_is_measurement_enabled || !thick_data.finite || !std::isfinite(thick_data.value) ||
        rclcpp::Time(thick_data.received_at) < _enabled_at)
    {
        return;
    }

    robot_interfaces::msg::ThicknessReading process_data = thick_data;
    _processed_data_publisher->publish(process_data);
    RCLCPP_INFO(get_logger(), "测量: seq=%" PRIu64 " value=%.9g um", thick_data.sequence,
                static_cast<double>(thick_data.value));
}

// 单元测试提供自己的 main；正常构建包含下方节点入口。
#ifndef MEASUREMENT_MANAGER_NO_MAIN
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
#endif
