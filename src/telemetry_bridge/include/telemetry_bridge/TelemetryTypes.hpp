#pragma once

#include "robot_common/clock/SteadyClock.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace telemetry_bridge
{

struct TelemetryConfig
{
    std::string device_path{"/dev/ttyTHS0"};
    unsigned int baud_rate{115200};
    std::size_t queue_capacity{64};
    std::uint32_t telemetry_max_age_ms{1000};
    std::uint32_t reconnect_interval_ms{1000};
};

struct PendingFrame
{
    std::vector<std::uint8_t> bytes;
    std::size_t write_offset{0};
    bool is_telemetry{false};
    robot_common::clock::SteadyClock::time_point queued_at;
};

struct RemoteMessage
{
    std::uint32_t message_id{0};
    std::uint8_t system_id{0};
    std::uint8_t component_id{0};
    std::vector<std::uint8_t> payload;
};

struct TelemetryStatus
{
    bool is_running{false};
    bool port_open{false};
    std::size_t queued_frames{0};
    std::uint64_t written_telemetry{0};
    std::uint64_t discarded_telemetry{0};
    std::uint64_t io_errors{0};
    std::string last_error;
};

} // namespace telemetry_bridge
