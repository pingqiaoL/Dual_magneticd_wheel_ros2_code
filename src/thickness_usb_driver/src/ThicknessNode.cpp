#include "thickness_usb_driver/ThicknessNode.hpp"
#include <rcl_interfaces/msg/parameter_descriptor.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

ThicknessNode::ThicknessNode() : Node("thickness_node")
{
    // 这里只保留启动选项。运行中修改设备或波特率需要重启节点。
    rcl_interfaces::msg::ParameterDescriptor startup;
    startup.read_only = true;
    startup.description = "启动参数；修改后重新启动节点";
    const auto device = declare_parameter<std::string>("device", "/dev/ttyACM0", startup);
    const auto baud = declare_parameter<std::int64_t>("baud_rate", 115200, startup);

    if (device.empty() || device.front() != '/' || device.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("device must be an absolute device path");
    }
    const std::int64_t supported[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    if (std::find(std::begin(supported), std::end(supported), baud) == std::end(supported))
    {
        throw std::invalid_argument("unsupported baud_rate");
    }

    // 厂家给出的通用广播/心跳请求载荷，不作为本台探头的业务SN。
    const std::string requestSerial = "EM231108C006";
    std::copy(requestSerial.begin(), requestSerial.end(), _requestSn.begin());
    _port = std::make_unique<UsbCdcPort>(device, static_cast<unsigned int>(baud));
    _readingPublisher = create_publisher<robot_interfaces::msg::ThicknessReading>(
        "/thickness/reading", rclcpp::QoS(64).reliable().durability_volatile());
    _statusPublisher = create_publisher<robot_interfaces::msg::ThicknessStatus>(
        "/thickness/status", rclcpp::QoS(1).reliable().durability_volatile());
    _timer = create_wall_timer(std::chrono::milliseconds(10), [this] { tick(); });

    RCLCPP_INFO(get_logger(), "采集节点已启动: device=%s baud=%lld unit=unknown",
        device.c_str(), static_cast<long long>(baud));
}

std::uint32_t ThicknessNode::ageMs(Clock::time_point now, Clock::time_point then)
{
    if (then == Clock::time_point{})
    {
        return std::numeric_limits<std::uint32_t>::max();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - then).count();
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(elapsed, 0, UINT32_MAX));
}

void ThicknessNode::disconnect(const std::string &reason)
{
    const bool report = _port->isOpen() || reason != _lastError;
    _lastError = reason;
    ++_ioErrors;
    _port->closePort();
    _protocol.clearBuffer();
    _identityKnown = false;
    _deviceSn.fill(0);
    _lastResponse = {};
    _lastMeasurement = {};
    _nextOpen = Clock::now() + std::chrono::milliseconds(ReconnectIntervalMs);
    if (report)
    {
        RCLCPP_WARN(get_logger(), "USB连接异常: %s (%s)", _port->devicePath().c_str(), reason.c_str());
    }
}

bool ThicknessNode::send(const std::vector<std::uint8_t> &bytes)
{
    const auto count = _port->writeBytes(bytes.data(), bytes.size(), WriteTimeoutMs);
    if (count != static_cast<ssize_t>(bytes.size()))
    {
        disconnect("request write incomplete: " + _port->lastError());
        return false;
    }
    return true;
}

void ThicknessNode::tick()
{
    auto now = Clock::now();
    if (!_port->isOpen() && now >= _nextOpen)
    {
        if (!_port->openPort())
        {
            disconnect(_port->lastError());
        }
        else
        {
            _protocol.clearBuffer();
            _identityKnown = false;
            _deviceSn.fill(0);
            _lastResponse = _lastMeasurement = _lastHeartbeat = _lastDiscovery = _lastQuery = {};
            _lastError.clear();
            RCLCPP_INFO(get_logger(), "已打开 %s", _port->devicePath().c_str());
        }
    }

    if (_port->isOpen())
    {
        std::uint8_t bytes[1024];
        // 每轮限制读取次数；readBytes(..., 0)只读取当前可用字节。
        for (int budget = 0; budget < 8; ++budget)
        {
            const auto count = _port->readBytes(bytes, sizeof(bytes), 0);
            if (count < 0)
            {
                disconnect(_port->lastError());
                break;
            }
            if (count == 0)
            {
                break;
            }
            std::vector<ThicknessFrame> frames;
            _protocol.add_data(bytes, static_cast<std::size_t>(count), frames);
            for (const auto &frame : frames)
            {
                receive(frame);
            }
        }

        now = Clock::now();
        if (_port->isOpen() && (!_identityKnown || ageMs(now, _lastResponse) > OnlineTimeoutMs) &&
            ageMs(now, _lastDiscovery) >= 3000)
        {
            _lastDiscovery = now;
            (void)send(ThicknessProtocol::create_message(MessageType::Broadcast, _requestSn));
        }
        if (_port->isOpen() && ageMs(now, _lastHeartbeat) >= HeartbeatPeriodMs)
        {
            _lastHeartbeat = now;
            (void)send(ThicknessProtocol::create_message(MessageType::Heartbeat, _requestSn));
        }
        if (_port->isOpen() && _identityKnown && ageMs(now, _lastQuery) >= StateQueryPeriodMs)
        {
            _lastQuery = now;
            (void)send(ThicknessProtocol::create_message(MessageType::StateQuery, _deviceSn));
        }
    }

    if (ageMs(now, _lastStatus) >= 500)
    {
        _lastStatus = now;
        publishStatus();
    }
}

void ThicknessNode::receive(const ThicknessFrame &frame)
{
    if (frame.company != 1 || frame.product != 3 || frame.status != 0 || frame.payload.size() < 12 ||
        !ThicknessProtocol::validSerial(frame.payload.data()))
    {
        return;
    }
    std::array<std::uint8_t, 12> serial;
    std::copy_n(frame.payload.begin(), serial.size(), serial.begin());
    if (_identityKnown && serial != _deviceSn)
    {
        return;
    }

    if (frame.function == 2 &&
        ((frame.category == 0xFF && frame.payload.size() == 25) ||
         (frame.category == 1 && frame.payload.size() == 12) ||
         (frame.category == 2 && frame.payload.size() == 20)))
    {
        _identityKnown = true;
        _deviceSn = serial;
        _lastResponse = Clock::now();
        _lastError.clear();
        return;
    }

    const auto measurement = ThicknessProtocol::measurement(frame);
    if (!measurement)
    {
        return;
    }
    _lastMeasurement = Clock::now();
    robot_interfaces::msg::ThicknessReading message;
    message.received_at = this->now();
    message.sequence = ++_sequence;
    message.device_sn = measurement->device_sn;
    message.value = measurement->value;
    message.unit = message.UNIT_UNKNOWN;
    message.material_raw = measurement->material_raw;
    message.finite = std::isfinite(measurement->value);
    _readingPublisher->publish(message);
}

void ThicknessNode::publishStatus()
{
    robot_interfaces::msg::ThicknessStatus status;
    const auto now = Clock::now();
    status.stamp = this->now();
    status.port_open = _port->isOpen();
    status.device_sn = _deviceSn;
    status.last_response_age_ms = ageMs(now, _lastResponse);
    status.last_measurement_age_ms = ageMs(now, _lastMeasurement);
    status.communication_ready = status.port_open && _identityKnown &&
        status.last_response_age_ms <= OnlineTimeoutMs;
    status.connection_state = !status.port_open ? status.DISCONNECTED :
        status.communication_ready ? status.ONLINE : _identityKnown ? status.STALE : status.CONNECTING;
    status.valid_frames = _protocol.stats().valid_frames;
    status.crc_errors = _protocol.stats().crc_errors;
    status.io_errors = _ioErrors;
    status.last_error = _lastError.substr(0, 160);
    _statusPublisher->publish(status);
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    try
    {
        rclcpp::spin(std::make_shared<ThicknessNode>());
    }
    catch (const std::exception &error)
    {
        RCLCPP_FATAL(rclcpp::get_logger("thickness_node"), "%s", error.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
