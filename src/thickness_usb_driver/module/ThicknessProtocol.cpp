#include "thickness_usb_driver/ThicknessProtocol.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

// 初始化环形缓冲区、最大报文长度
ThicknessProtocol::ThicknessProtocol(std::size_t capacity, std::size_t maxPayload)
    : _buffer(capacity), _maxPayload(maxPayload)
{
    if (maxPayload < 25 || maxPayload > 4096 || capacity < maxPayload + 28)
    {
        throw std::invalid_argument("invalid protocol capacity or maximum payload");
    }
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "protocol requires IEEE754 binary32");
}

// 添加新接收的数据
void ThicknessProtocol::add_data(const std::uint8_t *bytes, std::size_t count,
                                 std::vector<ThicknessFrame> &frames)
{
    if (bytes == nullptr && count != 0)
    {
        throw std::invalid_argument("null input");
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!_buffer.push(bytes[i]))
        {
            ++_stats.buffer_overflows;
            _stats.discarded_bytes += _buffer.size();
            _buffer.clear();
            (void)_buffer.push(bytes[i]);
        }
        while (extract(frames))
        {
        }
    }
}

// 提取完整报文
bool ThicknessProtocol::extract(std::vector<ThicknessFrame> &frames)
{
    if (_buffer.size() < 4)
    {
        return false;
    }
    const std::uint8_t head[4]{0xFE, 0xE1, 0x90, 0x0D};
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (_buffer.at(i) != head[i])
        {
            _buffer.discard(1);
            ++_stats.discarded_bytes;
            return true;
        }
    }
    if (_buffer.size() < 22)
    {
        return false;
    }
    const std::size_t length = (std::size_t(_buffer.at(20)) << 8) | _buffer.at(21);
    if (_buffer.at(4) != 0 || length > _maxPayload)
    {
        _buffer.discard(1);
        ++_stats.length_errors;
        return true;
    }
    const auto total = length + 28;
    if (_buffer.size() < total)
    {
        return false;
    }
    const std::uint8_t tail[4]{0x0D, 0x90, 0xE1, 0xFE};
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (_buffer.at(total - 4 + i) != tail[i])
        {
            _buffer.discard(1);
            ++_stats.framing_errors;
            return true;
        }
    }
    std::vector<std::uint8_t> payload(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        payload[i] = _buffer.at(22 + i);
    }
    const auto crc = crc16(payload.data(), payload.size());
    if (_buffer.at(22 + length) != (crc >> 8) || _buffer.at(23 + length) != (crc & 0xFF))
    {
        _buffer.discard(1);
        ++_stats.crc_errors;
        return true;
    }
    ThicknessFrame frame;
    frame.function = _buffer.at(5);
    frame.transaction = std::uint16_t((_buffer.at(6) << 8) | _buffer.at(7));
    frame.company = _buffer.at(8);
    frame.product = _buffer.at(9);
    frame.category = _buffer.at(10);
    frame.status = std::uint16_t((_buffer.at(18) << 8) | _buffer.at(19));
    frame.payload = std::move(payload);
    frames.push_back(std::move(frame));
    _buffer.discard(total);
    ++_stats.valid_frames;
    return true;
}

// 计算 CRC-16 校验值
std::uint16_t ThicknessProtocol::crc16(const std::uint8_t *bytes, std::size_t count)
{
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < count; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = std::uint16_t((crc >> 1) ^ ((crc & 1) ? 0xA001 : 0));
        }
    }
    return crc;
}

// 生成通用请求报文
std::vector<std::uint8_t>
ThicknessProtocol::create_general_message(std::uint8_t company, std::uint8_t product,
                                          std::uint8_t category,
                                          const std::array<std::uint8_t, 12> &serial)
{
    if (!validSerial(serial.data()))
    {
        throw std::invalid_argument("invalid request serial");
    }
    std::vector<std::uint8_t> request_message(40, 0);
    request_message[0] = 0xFE;
    request_message[1] = 0xE1;
    request_message[2] = 0x90;
    request_message[3] = 0x0D;
    request_message[5] = 1;
    request_message[8] = company;
    request_message[9] = product;
    request_message[10] = category;
    request_message[21] = 12;
    std::copy(serial.begin(), serial.end(), request_message.begin() + 22);
    const auto crc = crc16(serial.data(), serial.size());
    request_message[34] = std::uint8_t(crc >> 8);
    request_message[35] = std::uint8_t(crc);
    request_message[36] = 0x0D;
    request_message[37] = 0x90;
    request_message[38] = 0xE1;
    request_message[39] = 0xFE;
    return request_message;
}

// 生成用户选择的消息
std::vector<std::uint8_t>
ThicknessProtocol::create_message(MessageType type, const std::array<std::uint8_t, 12> &serial)
{
    switch (type)
    {
        case MessageType::Broadcast:
            return create_general_message(0xFF, 0xFF, 0xFF, serial);

        case MessageType::Heartbeat:
            return create_general_message(0xFF, 0xFF, 0x01, serial);

        case MessageType::StateQuery:
            return create_general_message(0x01, 0x03, 0x02, serial);

        case MessageType::TemperatureQuery:
            // 保留用户的候选构帧，尚未做实机协议确认；不接入自动发送流程。
            return create_general_message(0x01, 0x03, 0x06, serial);

        case MessageType::CalibrationUpdate:
            throw std::invalid_argument("CalibrationUpdate is not implemented");

        case MessageType::CalibrationConfirm:
            throw std::invalid_argument("CalibrationConfirm is not implemented");

        default:
            throw std::invalid_argument("Unknown message type");
    }
}

bool ThicknessProtocol::validSerial(const std::uint8_t *bytes)
{
    for (std::size_t i = 0; i < 12; ++i)
    {
        if (bytes[i] < 0x20 || bytes[i] > 0x7E)
        {
            return false;
        }
    }
    return true;
}

// 将 4 字节大端序数据转换成 float
float ThicknessProtocol::analyze_oridata(const std::uint8_t *bytes)
{
    const std::uint32_t bits = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
                               (std::uint32_t(bytes[2]) << 8) | bytes[3];
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// 将测量报文解析成厚度、材料类型等信息
std::optional<DecodedMeasurement> ThicknessProtocol::measurement(const ThicknessFrame &frame)
{
    if (frame.company != 1 || frame.product != 3 || frame.function != 0 || frame.category != 5 ||
        frame.status != 0 || frame.payload.size() != 17 || !validSerial(frame.payload.data()))
    {
        return {};
    }
    DecodedMeasurement measurement;
    std::copy_n(frame.payload.begin(), 12, measurement.device_sn.begin());
    measurement.material_raw = frame.payload[12];
    measurement.value = analyze_oridata(frame.payload.data() + 13);
    return measurement;
}

// 将状态响应解析成探头模式、校准厚度等信息
std::optional<DecodedProbeConfiguration>
ThicknessProtocol::configuration(const ThicknessFrame &frame)
{
    if (frame.company != 1 || frame.product != 3 || frame.function != 2 || frame.category != 2 ||
        frame.status != 0 || frame.payload.size() != 20 || !validSerial(frame.payload.data()))
    {
        return {};
    }
    DecodedProbeConfiguration config;
    std::copy_n(frame.payload.begin(), 12, config.device_sn.begin());
    config.current_probe = frame.payload[12];
    config.calibration_probe = frame.payload[13];
    config.menu_mode = frame.payload[14];
    config.device_probe = frame.payload[15];
    config.calibration_thickness = analyze_oridata(frame.payload.data() + 16);
    return config;
}
