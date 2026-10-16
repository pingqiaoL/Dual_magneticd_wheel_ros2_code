#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

inline constexpr std::size_t THICKNESS_SERIAL_NUMBER_BYTES = 12;

// 完整协议帧，尚未解释为测量值或仪器状态。
struct ThicknessFrame
{
    std::uint8_t function_code{};
    std::uint8_t company_id{};
    std::uint8_t product_id{};
    std::uint8_t category_code{};
    std::uint16_t transaction_id{};
    std::uint16_t status_code{};
    std::vector<std::uint8_t> payload;
};

struct DecodedMeasurement
{
    std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> device_serial_number{};
    float thickness_value{};
    std::uint8_t raw_material_code{};
};

struct DecodedProbeConfiguration
{
    std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> device_serial_number{};
    std::uint8_t current_probe_type{};
    std::uint8_t calibration_probe_type{};
    std::uint8_t menu_mode{};
    std::uint8_t configured_probe_type{};
    float calibration_thickness{};
};

struct ThicknessQuality
{
    std::uint64_t valid_frames{};
    std::uint64_t crc_errors{};
    std::uint64_t framing_errors{};
    std::uint64_t payload_length_errors{};
    std::uint64_t receive_buffer_overflows{};
    std::uint64_t discarded_bytes{};
};

enum class ThicknessConnectionState : std::uint8_t
{
    DISCONNECTED,
    CONNECTING,
    ONLINE,
    STALE
};

struct ThicknessDeviceConfig
{
    std::string device_path{"/dev/ttyACM0"};
    unsigned int baud_rate{115200};
    std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> request_serial_number{
        'E', 'M', '2', '3', '1', '1', '0', '8', 'C', '0', '0', '6'};
    std::uint32_t heartbeat_period_ms{1000};
    std::uint32_t online_timeout_ms{3000};
    std::uint32_t reconnect_interval_ms{1000};
    std::uint32_t discovery_period_ms{3000};
    std::uint32_t state_query_period_ms{5000};
    int write_timeout_ms{20};
};

struct ThicknessDeviceStatus
{
    ThicknessConnectionState connection_state{ThicknessConnectionState::DISCONNECTED};
    bool port_open{false};
    bool communication_ready{false};
    std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> device_serial_number{};
    std::uint32_t last_response_age_ms{};
    std::uint32_t last_measurement_age_ms{};
    ThicknessQuality quality;
    std::uint64_t io_error_count{};
    std::string last_error;
};
