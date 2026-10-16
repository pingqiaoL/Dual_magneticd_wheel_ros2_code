#include "thickness_usb_driver/ThicknessDevice.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace
{
constexpr int MAX_READ_ATTEMPTS_PER_CYCLE = 8;
constexpr std::size_t READ_BUFFER_BYTES = 1024;
}

ThicknessDevice::ThicknessDevice(ThicknessDeviceConfig config)
    : _config(std::move(config)), _serial_port(_config.device_path, _config.baud_rate)
{
    validate_config(_config);
}

void ThicknessDevice::validate_config(const ThicknessDeviceConfig &config)
{
    if (config.device_path.empty() || config.device_path.front() != '/' ||
        config.device_path.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("device_path must be an absolute device path");
    }
    if (!robot_common::io::SerialPort::is_supported_baud_rate(config.baud_rate))
    {
        throw std::invalid_argument("unsupported baud_rate");
    }
    if (!ThicknessProtocol::is_valid_serial_number(config.request_serial_number.data()))
    {
        throw std::invalid_argument("invalid request serial number");
    }
    if (config.heartbeat_period_ms == 0 || config.online_timeout_ms == 0 ||
        config.reconnect_interval_ms == 0 || config.discovery_period_ms == 0 ||
        config.state_query_period_ms == 0 || config.write_timeout_ms < 0)
    {
        throw std::invalid_argument("invalid thickness device timing config");
    }
}

void ThicknessDevice::process_io(std::vector<DecodedMeasurement> &measurements)
{
    measurements.clear();
    auto current_time = robot_common::clock::monotonic_time_now();
    open_if_due(current_time);

    if (!_serial_port.is_open())
    {
        return;
    }

    read_available(measurements);
    if (_serial_port.is_open())
    {
        current_time = robot_common::clock::monotonic_time_now();
        send_periodic_requests(current_time);
    }
}

void ThicknessDevice::open_if_due(SteadyClock::time_point current_time)
{
    if (_serial_port.is_open() || current_time < _next_open_time)
    {
        return;
    }

    if (!_serial_port.open_port())
    {
        handle_disconnect(_serial_port.last_error());
        return;
    }

    reset_session_state();
    _last_error.clear();
}

void ThicknessDevice::read_available(std::vector<DecodedMeasurement> &measurements)
{
    std::uint8_t read_buffer[READ_BUFFER_BYTES];
    for (int read_attempt = 0; read_attempt < MAX_READ_ATTEMPTS_PER_CYCLE; ++read_attempt)
    {
        const ssize_t bytes_read =
            _serial_port.read_bytes(read_buffer, sizeof(read_buffer), 0);
        if (bytes_read < 0)
        {
            handle_disconnect(_serial_port.last_error());
            return;
        }
        if (bytes_read == 0)
        {
            return;
        }

        std::vector<ThicknessFrame> decoded_frames;
        _protocol.feed_bytes(read_buffer, static_cast<std::size_t>(bytes_read), decoded_frames);
        for (const auto &frame : decoded_frames)
        {
            handle_frame(frame, measurements);
        }
    }
}

void ThicknessDevice::send_periodic_requests(SteadyClock::time_point current_time)
{
    const auto response_age_ms =
        robot_common::clock::elapsed_time_ms(current_time, _last_response_time);
    const auto discovery_age_ms =
        robot_common::clock::elapsed_time_ms(current_time, _last_discovery_time);
    const auto heartbeat_age_ms =
        robot_common::clock::elapsed_time_ms(current_time, _last_heartbeat_time);
    const auto query_age_ms =
        robot_common::clock::elapsed_time_ms(current_time, _last_query_time);
    const bool is_response_timed_out = response_age_ms > _config.online_timeout_ms;
    const bool should_discover_device = !_is_identity_known || is_response_timed_out;

    if (should_discover_device && discovery_age_ms >= _config.discovery_period_ms)
    {
        _last_discovery_time = current_time;
        if (!send_request(MessageType::BROADCAST, _config.request_serial_number))
        {
            return;
        }
    }

    if (heartbeat_age_ms >= _config.heartbeat_period_ms)
    {
        _last_heartbeat_time = current_time;
        if (!send_request(MessageType::HEARTBEAT, _config.request_serial_number))
        {
            return;
        }
    }

    if (_is_identity_known && query_age_ms >= _config.state_query_period_ms)
    {
        _last_query_time = current_time;
        (void)send_request(MessageType::STATE_QUERY, _device_serial_number);
    }
}

bool ThicknessDevice::send_request(
    MessageType message_type,
    const std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> &serial_number)
{
    const auto request_bytes = ThicknessProtocol::build_request(message_type, serial_number);
    const ssize_t bytes_written = _serial_port.write_bytes(
        request_bytes.data(), request_bytes.size(), _config.write_timeout_ms);
    if (bytes_written != static_cast<ssize_t>(request_bytes.size()))
    {
        handle_disconnect("request write incomplete: " + _serial_port.last_error());
        return false;
    }
    return true;
}

void ThicknessDevice::handle_frame(const ThicknessFrame &frame,
                                   std::vector<DecodedMeasurement> &measurements)
{
    if (frame.company_id != 1 || frame.product_id != 3 || frame.status_code != 0 ||
        frame.payload.size() < THICKNESS_SERIAL_NUMBER_BYTES ||
        !ThicknessProtocol::is_valid_serial_number(frame.payload.data()))
    {
        return;
    }

    std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> device_serial_number;
    std::copy_n(frame.payload.begin(), device_serial_number.size(), device_serial_number.begin());
    if (_is_identity_known && device_serial_number != _device_serial_number)
    {
        return;
    }

    const bool is_identity_response =
        frame.function_code == 2 &&
        ((frame.category_code == 0xFF && frame.payload.size() == 25) ||
         (frame.category_code == 1 && frame.payload.size() == 12) ||
         (frame.category_code == 2 && frame.payload.size() == 20));
    if (is_identity_response)
    {
        _is_identity_known = true;
        _device_serial_number = device_serial_number;
        _last_response_time = robot_common::clock::monotonic_time_now();
        _last_error.clear();
        return;
    }

    const auto measurement = ThicknessProtocol::decode_measurement(frame);
    if (!measurement)
    {
        return;
    }

    _last_measurement_time = robot_common::clock::monotonic_time_now();
    measurements.push_back(*measurement);
}

void ThicknessDevice::handle_disconnect(const std::string &reason)
{
    _last_error = reason;
    ++_io_error_count;
    _serial_port.close_port();
    reset_session_state();
    _next_open_time = robot_common::clock::monotonic_time_now() +
                      std::chrono::milliseconds(_config.reconnect_interval_ms);
}

void ThicknessDevice::reset_session_state()
{
    _protocol.clear_buffer();
    _is_identity_known = false;
    _device_serial_number.fill(0);
    _last_response_time = {};
    _last_measurement_time = {};
    _last_heartbeat_time = {};
    _last_discovery_time = {};
    _last_query_time = {};
}

ThicknessDeviceStatus ThicknessDevice::status() const
{
    ThicknessDeviceStatus device_status;
    const auto current_time = robot_common::clock::monotonic_time_now();
    device_status.port_open = _serial_port.is_open();
    device_status.device_serial_number = _device_serial_number;
    device_status.last_response_age_ms =
        robot_common::clock::elapsed_time_ms(current_time, _last_response_time);
    device_status.last_measurement_age_ms =
        robot_common::clock::elapsed_time_ms(current_time, _last_measurement_time);
    device_status.communication_ready =
        device_status.port_open && _is_identity_known &&
        device_status.last_response_age_ms <= _config.online_timeout_ms;

    if (!device_status.port_open)
    {
        device_status.connection_state = ThicknessConnectionState::DISCONNECTED;
    }
    else if (device_status.communication_ready)
    {
        device_status.connection_state = ThicknessConnectionState::ONLINE;
    }
    else if (_is_identity_known)
    {
        device_status.connection_state = ThicknessConnectionState::STALE;
    }
    else
    {
        device_status.connection_state = ThicknessConnectionState::CONNECTING;
    }

    device_status.quality = _protocol.quality();
    device_status.io_error_count = _io_error_count;
    device_status.last_error = _last_error;
    return device_status;
}

const ThicknessDeviceConfig &ThicknessDevice::config() const noexcept
{
    return _config;
}
