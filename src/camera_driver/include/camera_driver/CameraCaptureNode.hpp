#pragma once

#include "camera_driver/CameraDevice.hpp"

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace camera_driver
{
class CameraCaptureNode : public rclcpp::Node
{
  public:
    explicit CameraCaptureNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
    ~CameraCaptureNode() override;

  private:
    void publish_frame(std::unique_ptr<CameraFrame> frame);
    void publish_status();
    std::string _frame_id;
    std::unique_ptr<CameraDevice> _device;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr _image_publisher;
    rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr _info_publisher;
    rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr _status_publisher;
    rclcpp::TimerBase::SharedPtr _status_timer;
};
} // namespace camera_driver
