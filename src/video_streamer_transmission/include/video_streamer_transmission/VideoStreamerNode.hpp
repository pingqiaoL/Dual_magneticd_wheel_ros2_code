#pragma once

#include "video_streamer_transmission/VideoStream.hpp"

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_srvs/srv/set_bool.hpp>

namespace video_streamer_transmission
{
class VideoStreamerNode : public rclcpp::Node
{
  public:
    explicit VideoStreamerNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
    ~VideoStreamerNode() override;

  private:
    void on_image(sensor_msgs::msg::Image::ConstSharedPtr image);
    void publish_status();
    VideoConfig _config;
    std::unique_ptr<VideoStream> _stream;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr _image_subscription;
    rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr _status_publisher;
    rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr _enable_service;
    rclcpp::TimerBase::SharedPtr _status_timer;
};
} // namespace video_streamer_transmission
