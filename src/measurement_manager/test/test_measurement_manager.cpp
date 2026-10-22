#include "measurement_manager/MeasurementManagerNode.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

void check(bool condition, const char *reason)
{
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    int result_code = 0;
    try
    {
        auto manager = std::make_shared<MeasurementManagerNode>();
        auto observer = std::make_shared<rclcpp::Node>("measurement_manager_test");
        std::vector<robot_interfaces::msg::ThicknessReading> processed_data;
        auto publisher = observer->create_publisher<robot_interfaces::msg::ThicknessReading>(
            "/thickness/reading", 64);
        auto subscription = observer->create_subscription<robot_interfaces::msg::ThicknessReading>(
            "/measurement/thickness", 64,
            [&processed_data](const robot_interfaces::msg::ThicknessReading &thick_data)
            { processed_data.push_back(thick_data); });
        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(manager);
        executor.add_node(observer);
        const auto pump = [&executor](std::chrono::milliseconds duration)
        {
            const auto deadline = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < deadline)
            {
                executor.spin_some();
                std::this_thread::sleep_for(2ms);
            }
        };
        for (int attempt = 0; attempt < 100 && (publisher->get_subscription_count() == 0 ||
                                                subscription->get_publisher_count() == 0);
             ++attempt)
        {
            pump(50ms);
        }
        check(publisher->get_subscription_count() == 1, "reading subscription discovery");
        check(subscription->get_publisher_count() == 1, "processed data publisher discovery");
        robot_interfaces::msg::ThicknessReading reading;
        reading.value = 49;
        reading.finite = true;
        reading.unit = reading.UNIT_UM;
        const auto publish = [&]()
        {
            ++reading.sequence;
            publisher->publish(reading);
            pump(80ms);
        };
        reading.received_at = observer->now();
        publish();
        publish();
        check(processed_data.size() == 2, "default enabled and equal-valued new readings");
        check(processed_data.back().sequence == 2, "preserve reading metadata");
        pump(100ms);
        check(processed_data.size() == 2, "idle must not replay readings");

        manager->on_measurement_ready(false);
        reading.received_at = observer->now();
        publish();
        check(processed_data.size() == 2, "disabled must not publish");
        pump(10ms);
        manager->on_measurement_ready(true);
        publish();
        check(processed_data.size() == 2, "reject reading received before enable");
        reading.received_at = observer->now();
        pump(10ms);
        manager->on_measurement_ready(true);
        publish();
        check(processed_data.size() == 3, "repeated ready must not move enabled timestamp");
        reading.received_at = observer->now();
        reading.value = std::numeric_limits<float>::quiet_NaN();
        publish();
        reading.value = std::numeric_limits<float>::infinity();
        publish();
        reading.value = 50;
        reading.finite = false;
        publish();
        check(processed_data.size() == 3, "reject invalid floating-point values");
        manager->on_measurement_ready(false);
        manager->on_measurement_ready(false);
        publish();
        check(processed_data.size() == 3, "repeated cancellation");
        std::cout << "PASS default enabled, continuous, equal values, idle, cancel/resume, "
                     "old input, repeated ready, invalid values\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result_code = 1;
    }
    rclcpp::shutdown();
    return result_code;
}
