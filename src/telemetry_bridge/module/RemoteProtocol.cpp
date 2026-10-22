#include "telemetry_bridge/RemoteProtocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>

namespace telemetry_bridge
{
namespace
{
constexpr std::uint8_t LOCAL_SYSTEM_ID = 1;
constexpr std::uint8_t LOCAL_COMPONENT_ID = 191;
constexpr std::uint8_t MAVLINK_1_MAGIC = 0xFE;
constexpr std::uint8_t MAVLINK_2_MAGIC = 0xFD;
constexpr std::uint8_t MAV_RESULT_UNSUPPORTED = 3;

struct MessageDefinition
{
    std::uint8_t crc_extra;
    std::size_t payload_size;
};

std::optional<MessageDefinition> message_definition(std::uint32_t message_id)
{
    switch (message_id)
    {
        case 0:
            return MessageDefinition{50, 9};
        case 4:
            return MessageDefinition{237, 14};
        case 76:
            return MessageDefinition{152, 33};
        case 77:
            return MessageDefinition{143, 10};
        case 251:
            return MessageDefinition{170, 18};
        case 252:
            return MessageDefinition{44, 18};
        default:
            return std::nullopt;
    }
}

std::uint16_t accumulate_crc(std::uint16_t crc, std::uint8_t byte)
{
    std::uint8_t temporary = byte ^ static_cast<std::uint8_t>(crc);
    temporary ^= static_cast<std::uint8_t>(temporary << 4);
    return static_cast<std::uint16_t>((crc >> 8) ^ (std::uint16_t(temporary) << 8) ^
                                      (std::uint16_t(temporary) << 3) ^ (temporary >> 4));
}

void store_uint32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t byte_index = 0; byte_index < 4; ++byte_index)
    {
        bytes[offset + byte_index] = static_cast<std::uint8_t>(value >> (8 * byte_index));
    }
}

std::vector<std::uint8_t> named_payload(std::uint32_t time_boot_ms, const std::string &name)
{
    const bool has_non_ascii_character =
        std::any_of(name.begin(), name.end(),
                    [](unsigned char character) { return character < 0x20 || character > 0x7E; });
    if (name.empty() || name.size() > 10 || has_non_ascii_character)
    {
        throw std::invalid_argument("MAVLink name must contain 1..10 printable ASCII bytes");
    }
    std::vector<std::uint8_t> payload(18, 0);
    store_uint32(payload, 0, time_boot_ms);
    std::copy(name.begin(), name.end(), payload.begin() + 8);
    return payload;
}

bool targets_local(std::uint8_t system_id, std::uint8_t component_id)
{
    return (system_id == 0 || system_id == LOCAL_SYSTEM_ID) &&
           (component_id == 0 || component_id == LOCAL_COMPONENT_ID);
}
} // namespace

std::vector<std::uint8_t> RemoteProtocol::build_frame(std::uint8_t sequence,
                                                      std::uint32_t message_id,
                                                      const std::vector<std::uint8_t> &payload)
{
    const auto definition = message_definition(message_id);
    if (!definition || payload.size() > 255)
    {
        throw std::invalid_argument("unsupported MAVLink frame");
    }
    std::vector<std::uint8_t> frame{MAVLINK_2_MAGIC,
                                    static_cast<std::uint8_t>(payload.size()),
                                    0,
                                    0,
                                    sequence,
                                    LOCAL_SYSTEM_ID,
                                    LOCAL_COMPONENT_ID,
                                    static_cast<std::uint8_t>(message_id),
                                    static_cast<std::uint8_t>(message_id >> 8),
                                    static_cast<std::uint8_t>(message_id >> 16)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    std::uint16_t crc = 0xFFFF;
    for (std::size_t byte_index = 1; byte_index < frame.size(); ++byte_index)
    {
        crc = accumulate_crc(crc, frame[byte_index]);
    }
    crc = accumulate_crc(crc, definition->crc_extra);
    frame.push_back(static_cast<std::uint8_t>(crc));
    frame.push_back(static_cast<std::uint8_t>(crc >> 8));
    return frame;
}

std::vector<std::uint8_t> RemoteProtocol::heartbeat(std::uint8_t sequence)
{
    // MAV_TYPE_ONBOARD_CONTROLLER=18, AUTOPILOT_INVALID=8, 未声明车辆解锁状态。
    return build_frame(sequence, 0, {0, 0, 0, 0, 18, 8, 0, 3, 3});
}

void RemoteProtocol::set_sequence(std::vector<std::uint8_t> &frame, std::uint8_t sequence)
{
    if (frame.size() < 12 || frame[0] != MAVLINK_2_MAGIC)
    {
        throw std::invalid_argument("invalid outgoing MAVLink frame");
    }
    const auto definition = message_definition(
        std::uint32_t(frame[7]) | (std::uint32_t(frame[8]) << 8) | (std::uint32_t(frame[9]) << 16));
    if (!definition)
    {
        throw std::invalid_argument("unknown outgoing MAVLink message");
    }
    frame[4] = sequence;
    std::uint16_t crc = 0xFFFF;
    for (std::size_t offset = 1; offset < frame.size() - 2; ++offset)
    {
        crc = accumulate_crc(crc, frame[offset]);
    }
    crc = accumulate_crc(crc, definition->crc_extra);
    frame[frame.size() - 2] = static_cast<std::uint8_t>(crc);
    frame[frame.size() - 1] = static_cast<std::uint8_t>(crc >> 8);
}

std::vector<std::uint8_t> RemoteProtocol::named_float(std::uint8_t sequence,
                                                      std::uint32_t time_boot_ms,
                                                      const std::string &name, float value)
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    auto payload = named_payload(time_boot_ms, name);
    std::uint32_t value_bits;
    std::memcpy(&value_bits, &value, sizeof(value_bits));
    store_uint32(payload, 4, value_bits);
    return build_frame(sequence, 251, payload);
}

std::vector<std::uint8_t> RemoteProtocol::named_int(std::uint8_t sequence,
                                                    std::uint32_t time_boot_ms,
                                                    const std::string &name, std::int32_t value)
{
    auto payload = named_payload(time_boot_ms, name);
    store_uint32(payload, 4, static_cast<std::uint32_t>(value));
    return build_frame(sequence, 252, payload);
}

std::vector<std::uint8_t> RemoteProtocol::ping_reply(std::uint8_t sequence,
                                                     const RemoteMessage &request)
{
    if (request.message_id != 4 || request.payload.size() < 14 ||
        !targets_local(request.payload[12], request.payload[13]))
    {
        return {};
    }
    auto payload = request.payload;
    payload.resize(14);
    payload[12] = request.system_id;
    payload[13] = request.component_id;
    return build_frame(sequence, 4, payload);
}

std::vector<std::uint8_t> RemoteProtocol::unsupported_command_ack(std::uint8_t sequence,
                                                                  const RemoteMessage &request)
{
    if (request.message_id != 76 || request.payload.size() < 33 ||
        !targets_local(request.payload[30], request.payload[31]))
    {
        return {};
    }
    std::vector<std::uint8_t> payload(10, 0);
    payload[0] = request.payload[28];
    payload[1] = request.payload[29];
    payload[2] = MAV_RESULT_UNSUPPORTED;
    payload[8] = request.system_id;
    payload[9] = request.component_id;
    return build_frame(sequence, 77, payload);
}

void RemoteProtocol::feed_bytes(const std::uint8_t *bytes, std::size_t byte_count,
                                std::vector<RemoteMessage> &messages)
{
    if (bytes == nullptr && byte_count != 0)
    {
        throw std::invalid_argument("null MAVLink input");
    }
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        _receive_buffer.push_back(bytes[byte_index]);
        while (!_receive_buffer.empty())
        {
            const bool is_v2 = _receive_buffer.front() == MAVLINK_2_MAGIC;
            if (!is_v2 && _receive_buffer.front() != MAVLINK_1_MAGIC)
            {
                _receive_buffer.erase(_receive_buffer.begin());
                continue;
            }
            const std::size_t header_size = is_v2 ? 10 : 6;
            if (_receive_buffer.size() < header_size)
            {
                break;
            }
            // 当前未配置签名密钥，拒绝任何不兼容标志，不把 CRC 当签名验证。
            if (is_v2 && _receive_buffer[2] != 0)
            {
                _receive_buffer.erase(_receive_buffer.begin());
                continue;
            }
            const std::size_t payload_size = _receive_buffer[1];
            const std::size_t frame_size = header_size + payload_size + 2;
            const std::uint32_t message_id = is_v2 ? std::uint32_t(_receive_buffer[7]) |
                                                         (std::uint32_t(_receive_buffer[8]) << 8) |
                                                         (std::uint32_t(_receive_buffer[9]) << 16)
                                                   : _receive_buffer[5];
            const auto definition = message_definition(message_id);
            if (!definition || payload_size > definition->payload_size)
            {
                _receive_buffer.erase(_receive_buffer.begin());
                continue;
            }
            if (_receive_buffer.size() < frame_size)
            {
                break;
            }
            std::uint16_t crc = 0xFFFF;
            for (std::size_t offset = 1; offset < frame_size - 2; ++offset)
            {
                crc = accumulate_crc(crc, _receive_buffer[offset]);
            }
            crc = accumulate_crc(crc, definition->crc_extra);
            const std::uint16_t received_crc =
                _receive_buffer[frame_size - 2] |
                (std::uint16_t(_receive_buffer[frame_size - 1]) << 8);
            if (crc != received_crc || (!is_v2 && payload_size == 0))
            {
                _receive_buffer.erase(_receive_buffer.begin());
                continue;
            }
            RemoteMessage message;
            message.message_id = message_id;
            message.system_id = _receive_buffer[is_v2 ? 5 : 3];
            message.component_id = _receive_buffer[is_v2 ? 6 : 4];
            message.payload.assign(_receive_buffer.begin() + header_size,
                                   _receive_buffer.begin() + header_size + payload_size);
            // MAVLink 2 尾部零截断：接收时补零，再按标准字段偏移解析。
            message.payload.resize(definition->payload_size, 0);
            messages.push_back(std::move(message));
            _receive_buffer.erase(_receive_buffer.begin(), _receive_buffer.begin() + frame_size);
        }
    }
}

void RemoteProtocol::reset()
{
    _receive_buffer.clear();
}

} // namespace telemetry_bridge
