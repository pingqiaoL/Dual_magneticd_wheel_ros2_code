#pragma once

#include "thickness_usb_driver/ThicknessProtocol.hpp"
#include "thickness_usb_driver/UsbCdcPort.hpp"
#include <rclcpp/rclcpp.hpp>
#include <robot_interfaces/msg/thickness_reading.hpp>
#include <robot_interfaces/msg/thickness_status.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 采集节点只负责USB收发、协议解析和发布；端口始终由同一个线程访问。
class ThicknessNode final : public rclcpp::Node
{
public:
    ThicknessNode();

private:
    using Clock = std::chrono::steady_clock;

    // 当前先使用固定通信时序，不引入动态参数更新系统。
    static constexpr int HeartbeatPeriodMs = 1000;
    static constexpr int OnlineTimeoutMs = 3000;
    static constexpr int ReconnectIntervalMs = 1000;
    static constexpr int StateQueryPeriodMs = 5000;
    static constexpr int WriteTimeoutMs = 20;

    static std::uint32_t ageMs(Clock::time_point now, Clock::time_point then);
    void tick();
    bool send(const std::vector<std::uint8_t> &bytes);
    void receive(const ThicknessFrame &frame);
    void publishStatus();
    void disconnect(const std::string &reason);

    std::unique_ptr<UsbCdcPort> _port;
    ThicknessProtocol _protocol;
    std::array<std::uint8_t, 12> _requestSn{}, _deviceSn{};
    bool _identityKnown{false};
    std::uint64_t _sequence{0}, _ioErrors{0};
    std::string _lastError;
    Clock::time_point _nextOpen{}, _lastResponse{}, _lastMeasurement{};
    Clock::time_point _lastHeartbeat{}, _lastDiscovery{}, _lastQuery{}, _lastStatus{};

    rclcpp::Publisher<robot_interfaces::msg::ThicknessReading>::SharedPtr _readingPublisher;
    rclcpp::Publisher<robot_interfaces::msg::ThicknessStatus>::SharedPtr _statusPublisher;
    rclcpp::TimerBase::SharedPtr _timer;
};
