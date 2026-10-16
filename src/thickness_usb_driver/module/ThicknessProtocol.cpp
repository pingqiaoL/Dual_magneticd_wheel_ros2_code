#include "thickness_usb_driver/ThicknessProtocol.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
constexpr std::array<std::uint8_t, 4> FRAME_HEADER{0xFE, 0xE1, 0x90, 0x0D};
constexpr std::array<std::uint8_t, 4> FRAME_TAIL{0x0D, 0x90, 0xE1, 0xFE};
constexpr std::size_t VERSION_OFFSET = 4;
constexpr std::size_t FUNCTION_CODE_OFFSET = 5;
constexpr std::size_t TRANSACTION_ID_HIGH_OFFSET = 6;
constexpr std::size_t TRANSACTION_ID_LOW_OFFSET = 7;
constexpr std::size_t COMPANY_ID_OFFSET = 8;
constexpr std::size_t PRODUCT_ID_OFFSET = 9;
constexpr std::size_t CATEGORY_CODE_OFFSET = 10;
constexpr std::size_t STATUS_CODE_HIGH_OFFSET = 18;
constexpr std::size_t STATUS_CODE_LOW_OFFSET = 19;
constexpr std::size_t FIXED_FIELDS_BYTES = 22;
constexpr std::size_t FRAME_OVERHEAD_BYTES = 28;
constexpr std::size_t PAYLOAD_LENGTH_HIGH_OFFSET = 20;
constexpr std::size_t PAYLOAD_LENGTH_LOW_OFFSET = 21;
constexpr std::size_t PAYLOAD_OFFSET = 22;
constexpr std::size_t MIN_MAX_PAYLOAD_BYTES = 25;
constexpr std::size_t MAX_MAX_PAYLOAD_BYTES = 4096;
} // namespace

// 初始化环形缓冲区、最大报文长度
ThicknessProtocol::ThicknessProtocol(std::size_t buffer_capacity, std::size_t max_payload_size)
    : _receive_buffer(buffer_capacity), _max_payload_size(max_payload_size)
{
    if (max_payload_size < MIN_MAX_PAYLOAD_BYTES ||
        max_payload_size > MAX_MAX_PAYLOAD_BYTES ||
        buffer_capacity < max_payload_size + FRAME_OVERHEAD_BYTES)
    {
        throw std::invalid_argument("invalid protocol capacity or maximum payload");
    }
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "protocol requires IEEE754 binary32");
}

// 添加新接收的数据
void ThicknessProtocol::feed_bytes(const std::uint8_t *bytes, std::size_t byte_count,
                                   std::vector<ThicknessFrame> &decoded_frames)
{
    if (bytes == nullptr && byte_count != 0)
    {
        throw std::invalid_argument("null input");
    }
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        if (!_receive_buffer.push_byte(bytes[byte_index]))
        {
            ++_quality.receive_buffer_overflows;
            _quality.discarded_bytes += _receive_buffer.size();
            _receive_buffer.clear();
            (void)_receive_buffer.push_byte(bytes[byte_index]);
        }
        while (extract_frame(decoded_frames))
        {
        }
    }
}

// 提取完整报文
bool ThicknessProtocol::extract_frame(std::vector<ThicknessFrame> &decoded_frames)
{
    if (_receive_buffer.size() < 4)
    {
        return false;
    }
    for (std::size_t byte_index = 0; byte_index < FRAME_HEADER.size(); ++byte_index)
    {
        if (_receive_buffer.byte_at(byte_index) != FRAME_HEADER[byte_index])
        {
            _receive_buffer.discard_bytes(1);
            ++_quality.discarded_bytes;
            return true;
        }
    }
    if (_receive_buffer.size() < FIXED_FIELDS_BYTES)
    {
        return false;
    }
    const std::size_t payload_length =
        (static_cast<std::size_t>(_receive_buffer.byte_at(PAYLOAD_LENGTH_HIGH_OFFSET)) << 8) |
        _receive_buffer.byte_at(PAYLOAD_LENGTH_LOW_OFFSET);
    if (_receive_buffer.byte_at(VERSION_OFFSET) != 0 || payload_length > _max_payload_size)
    {
        _receive_buffer.discard_bytes(1);
        ++_quality.payload_length_errors;
        return true;
    }
    const std::size_t frame_length = payload_length + FRAME_OVERHEAD_BYTES;
    if (_receive_buffer.size() < frame_length)
    {
        return false;
    }
    for (std::size_t byte_index = 0; byte_index < FRAME_TAIL.size(); ++byte_index)
    {
        if (_receive_buffer.byte_at(frame_length - FRAME_TAIL.size() + byte_index) !=
            FRAME_TAIL[byte_index])
        {
            _receive_buffer.discard_bytes(1);
            ++_quality.framing_errors;
            return true;
        }
    }
    std::vector<std::uint8_t> payload(payload_length);
    for (std::size_t byte_index = 0; byte_index < payload_length; ++byte_index)
    {
        payload[byte_index] = _receive_buffer.byte_at(PAYLOAD_OFFSET + byte_index);
    }
    const std::uint16_t calculated_crc = calculate_crc16(payload.data(), payload.size());
    if (_receive_buffer.byte_at(PAYLOAD_OFFSET + payload_length) != (calculated_crc >> 8) ||
        _receive_buffer.byte_at(PAYLOAD_OFFSET + payload_length + 1) !=
            (calculated_crc & 0xFF))
    {
        _receive_buffer.discard_bytes(1);
        ++_quality.crc_errors;
        return true;
    }
    ThicknessFrame frame;
    frame.function_code = _receive_buffer.byte_at(FUNCTION_CODE_OFFSET);
    frame.transaction_id =
        std::uint16_t((_receive_buffer.byte_at(TRANSACTION_ID_HIGH_OFFSET) << 8) |
                      _receive_buffer.byte_at(TRANSACTION_ID_LOW_OFFSET));
    frame.company_id = _receive_buffer.byte_at(COMPANY_ID_OFFSET);
    frame.product_id = _receive_buffer.byte_at(PRODUCT_ID_OFFSET);
    frame.category_code = _receive_buffer.byte_at(CATEGORY_CODE_OFFSET);
    frame.status_code =
        std::uint16_t((_receive_buffer.byte_at(STATUS_CODE_HIGH_OFFSET) << 8) |
                      _receive_buffer.byte_at(STATUS_CODE_LOW_OFFSET));
    frame.payload = std::move(payload);
    decoded_frames.push_back(std::move(frame));
    _receive_buffer.discard_bytes(frame_length);
    ++_quality.valid_frames;
    return true;
}

// 计算 CRC-16 校验值
std::uint16_t ThicknessProtocol::calculate_crc16(const std::uint8_t *bytes, std::size_t byte_count)
{
    std::uint16_t crc = 0xFFFF;
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        crc ^= bytes[byte_index];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = std::uint16_t((crc >> 1) ^ ((crc & 1) ? 0xA001 : 0));
        }
    }
    return crc;
}

// 生成通用请求报文
std::vector<std::uint8_t>
ThicknessProtocol::build_serial_request(std::uint8_t company_id, std::uint8_t product_id,
                                        std::uint8_t category_code,
                                        const std::array<std::uint8_t,
                                                         THICKNESS_SERIAL_NUMBER_BYTES> &serial_number)
{
    if (!is_valid_serial_number(serial_number.data()))
    {
        throw std::invalid_argument("invalid request serial");
    }
    std::vector<std::uint8_t> request_frame(THICKNESS_SERIAL_NUMBER_BYTES + FRAME_OVERHEAD_BYTES, 0);
    std::copy(FRAME_HEADER.begin(), FRAME_HEADER.end(), request_frame.begin());
    request_frame[FUNCTION_CODE_OFFSET] = 1;
    request_frame[COMPANY_ID_OFFSET] = company_id;
    request_frame[PRODUCT_ID_OFFSET] = product_id;
    request_frame[CATEGORY_CODE_OFFSET] = category_code;
    request_frame[PAYLOAD_LENGTH_LOW_OFFSET] = THICKNESS_SERIAL_NUMBER_BYTES;
    std::copy(serial_number.begin(), serial_number.end(), request_frame.begin() + PAYLOAD_OFFSET);
    const std::uint16_t calculated_crc =
        calculate_crc16(serial_number.data(), serial_number.size());
    const std::size_t crc_offset = PAYLOAD_OFFSET + serial_number.size();
    request_frame[crc_offset] = std::uint8_t(calculated_crc >> 8);
    request_frame[crc_offset + 1] = std::uint8_t(calculated_crc);
    std::copy(FRAME_TAIL.begin(), FRAME_TAIL.end(), request_frame.end() - FRAME_TAIL.size());
    return request_frame;
}

// 生成用户选择的消息
std::vector<std::uint8_t>
ThicknessProtocol::build_request(MessageType message_type,
                                 const std::array<std::uint8_t,
                                                  THICKNESS_SERIAL_NUMBER_BYTES> &serial_number)
{
    switch (message_type)
    {
        case MessageType::BROADCAST:
            return build_serial_request(0xFF, 0xFF, 0xFF, serial_number);

        case MessageType::HEARTBEAT:
            return build_serial_request(0xFF, 0xFF, 0x01, serial_number);

        case MessageType::STATE_QUERY:
            return build_serial_request(0x01, 0x03, 0x02, serial_number);

        case MessageType::TEMPERATURE_QUERY:
            // 保留用户的候选构帧，尚未做实机协议确认；不接入自动发送流程。
            return build_serial_request(0x01, 0x03, 0x06, serial_number);

        case MessageType::CALIBRATION_UPDATE:
            throw std::invalid_argument("CalibrationUpdate is not implemented");

        case MessageType::CALIBRATION_CONFIRM:
            throw std::invalid_argument("CalibrationConfirm is not implemented");

        default:
            throw std::invalid_argument("Unknown message type");
    }
}

bool ThicknessProtocol::is_valid_serial_number(const std::uint8_t *bytes)
{
    if (bytes == nullptr)
    {
        return false;
    }
    for (std::size_t byte_index = 0; byte_index < THICKNESS_SERIAL_NUMBER_BYTES; ++byte_index)
    {
        if (bytes[byte_index] < 0x20 || bytes[byte_index] > 0x7E)
        {
            return false;
        }
    }
    return true;
}

// 将 4 字节大端序数据转换成 float
float ThicknessProtocol::decode_float32_be(const std::uint8_t *bytes)
{
    const std::uint32_t float_bits = (std::uint32_t(bytes[0]) << 24) |
                                     (std::uint32_t(bytes[1]) << 16) |
                                     (std::uint32_t(bytes[2]) << 8) | bytes[3];
    float decoded_value;
    std::memcpy(&decoded_value, &float_bits, sizeof(decoded_value));
    return decoded_value;
}

// 将测量报文解析成厚度、材料类型等信息
std::optional<DecodedMeasurement> ThicknessProtocol::decode_measurement(const ThicknessFrame &frame)
{
    if (frame.company_id != 1 || frame.product_id != 3 || frame.function_code != 0 ||
        frame.category_code != 5 || frame.status_code != 0 || frame.payload.size() != 17 ||
        !is_valid_serial_number(frame.payload.data()))
    {
        return {};
    }
    DecodedMeasurement measurement;
    std::copy_n(frame.payload.begin(), THICKNESS_SERIAL_NUMBER_BYTES,
                measurement.device_serial_number.begin());
    measurement.raw_material_code = frame.payload[THICKNESS_SERIAL_NUMBER_BYTES];
    measurement.thickness_value =
        decode_float32_be(frame.payload.data() + THICKNESS_SERIAL_NUMBER_BYTES + 1);
    return measurement;
}

// 将状态响应解析成探头模式、校准厚度等信息
std::optional<DecodedProbeConfiguration>
ThicknessProtocol::decode_probe_config(const ThicknessFrame &frame)
{
    if (frame.company_id != 1 || frame.product_id != 3 || frame.function_code != 2 ||
        frame.category_code != 2 || frame.status_code != 0 || frame.payload.size() != 20 ||
        !is_valid_serial_number(frame.payload.data()))
    {
        return {};
    }
    DecodedProbeConfiguration config;
    std::copy_n(frame.payload.begin(), THICKNESS_SERIAL_NUMBER_BYTES,
                config.device_serial_number.begin());
    config.current_probe_type = frame.payload[THICKNESS_SERIAL_NUMBER_BYTES];
    config.calibration_probe_type = frame.payload[THICKNESS_SERIAL_NUMBER_BYTES + 1];
    config.menu_mode = frame.payload[THICKNESS_SERIAL_NUMBER_BYTES + 2];
    config.configured_probe_type = frame.payload[THICKNESS_SERIAL_NUMBER_BYTES + 3];
    config.calibration_thickness =
        decode_float32_be(frame.payload.data() + THICKNESS_SERIAL_NUMBER_BYTES + 4);
    return config;
}
