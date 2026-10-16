#pragma once

#include "robot_common/clock/SteadyClock.hpp"
#include "robot_common/io/SerialPort.hpp"
#include "thickness_usb_driver/ThicknessProtocol.hpp"
#include "thickness_usb_driver/ThicknessTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

/**
 * 管理一台测厚仪的完整通信会话。
 *
 * 本类不依赖 ROS。调用 process_io() 时，同步完成有界读取、报文解析、
 * 设备发现、心跳、状态查询和断线恢复。所有方法由同一线程调用。
 */
class ThicknessDevice final
{
  public:
    explicit ThicknessDevice(ThicknessDeviceConfig config);

    // 清空 measurements，然后写入本轮收到的有效测量数据。
    void process_io(std::vector<DecodedMeasurement> &measurements);

    // 返回调用时刻的端口状态、通信状态、数据年龄和错误统计。
    ThicknessDeviceStatus status() const;
    const ThicknessDeviceConfig &config() const noexcept;

  private:
    using SteadyClock = robot_common::clock::SteadyClock;

    static void validate_config(const ThicknessDeviceConfig &config);
    void open_if_due(SteadyClock::time_point current_time);
    void read_available(std::vector<DecodedMeasurement> &measurements);
    void send_periodic_requests(SteadyClock::time_point current_time);
    bool send_request(
        MessageType message_type,
        const std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> &serial_number);
    void handle_frame(const ThicknessFrame &frame,
                      std::vector<DecodedMeasurement> &measurements);
    void handle_disconnect(const std::string &reason);
    void reset_session_state();

    ThicknessDeviceConfig _config;
    robot_common::io::SerialPort _serial_port;
    ThicknessProtocol _protocol;
    std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> _device_serial_number{};
    bool _is_identity_known{false};
    std::uint64_t _io_error_count{0};
    std::string _last_error;
    SteadyClock::time_point _next_open_time{};
    SteadyClock::time_point _last_response_time{};
    SteadyClock::time_point _last_measurement_time{};
    SteadyClock::time_point _last_heartbeat_time{};
    SteadyClock::time_point _last_discovery_time{};
    SteadyClock::time_point _last_query_time{};
};
