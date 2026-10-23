#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace camera_driver
{
// 视觉分支按需转换，不在采集线程或编码回调中执行颜色转换。
class BgrConverterNode final : public rclcpp::Node
{
  public:
    explicit BgrConverterNode(const rclcpp::NodeOptions &options) : Node("camera_bgr_node", options)
    {
        _publisher = create_publisher<sensor_msgs::msg::Image>(
            "/camera/image_raw", rclcpp::SensorDataQoS().keep_last(1));
        _subscription = create_subscription<sensor_msgs::msg::Image>(
            "/camera/image_yuv", rclcpp::SensorDataQoS().keep_last(1),
            [this](sensor_msgs::msg::Image::ConstSharedPtr image) { convert_image(image); });
    }

  private:
    void convert_image(const sensor_msgs::msg::Image::ConstSharedPtr &image)
    {
        if (_publisher->get_subscription_count() == 0 &&
            _publisher->get_intra_process_subscription_count() == 0)
            return;
        if (image->encoding != "yuv420" || image->width % 2 || image->height % 2 ||
            image->width == 0 || image->height == 0 || image->step != image->width ||
            image->data.size() != static_cast<std::size_t>(image->width) * image->height * 3 / 2)
            return;
        auto output = std::make_unique<sensor_msgs::msg::Image>();
        output->header = image->header;
        output->width = image->width;
        output->height = image->height;
        output->encoding = "bgr8";
        output->step = image->width * 3;
        output->data.resize(static_cast<std::size_t>(output->step) * output->height);
        cv::Mat yuv(image->height * 3 / 2, image->width, CV_8UC1,
                    const_cast<std::uint8_t *>(image->data.data()));
        cv::Mat bgr(output->height, output->width, CV_8UC3, output->data.data());
        cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);
        _publisher->publish(std::move(output));
    }

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr _publisher;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr _subscription;
};
} // namespace camera_driver
RCLCPP_COMPONENTS_REGISTER_NODE(camera_driver::BgrConverterNode)
