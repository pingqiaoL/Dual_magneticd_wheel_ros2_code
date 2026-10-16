#pragma once

#include "robot_common/buffer/ByteRingBuffer.hpp"
#include "thickness_usb_driver/ThicknessTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

enum class MessageType
{
    BROADCAST,
    HEARTBEAT,
    STATE_QUERY,
    CALIBRATION_UPDATE,  // 预留：当前不支持，build_request()会明确抛出异常。
    CALIBRATION_CONFIRM, // 预留：当前不支持，build_request()会明确抛出异常。
    TEMPERATURE_QUERY    // 候选类别0x06，仅构帧；未实机验证，采集节点不调用。
};

// 普通C++协议库，不依赖ROS，不打开设备、不发送校准或更新命令。
class ThicknessProtocol final
{
  public:
    explicit ThicknessProtocol(std::size_t buffer_capacity = 4096,
                               std::size_t max_payload_size = 128);
    void feed_bytes(const std::uint8_t *bytes, std::size_t byte_count,
                    std::vector<ThicknessFrame> &decoded_frames);
    void clear_buffer() noexcept
    {
        _receive_buffer.clear();
    }
    const ThicknessQuality &quality() const noexcept
    {
        return _quality;
    }
    static std::uint16_t calculate_crc16(const std::uint8_t *bytes, std::size_t byte_count);
    // 广播/心跳使用厂家请求SN；已发现设备的状态查询使用实际业务SN。
    static std::vector<std::uint8_t>
    build_request(MessageType message_type,
                  const std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> &serial_number);

    static bool is_valid_serial_number(const std::uint8_t *bytes);
    static std::optional<DecodedMeasurement> decode_measurement(const ThicknessFrame &frame);
    static std::optional<DecodedProbeConfiguration>
    decode_probe_config(const ThicknessFrame &frame);

  private:
    bool extract_frame(std::vector<ThicknessFrame> &decoded_frames);
    static float decode_float32_be(const std::uint8_t *bytes);
    // 仅生成“功能01 + 12字节SN载荷”的请求，不能用于携带校准数据的写入。
    static std::vector<std::uint8_t>
    build_serial_request(std::uint8_t company_id, std::uint8_t product_id,
                         std::uint8_t category_code,
                         const std::array<std::uint8_t, THICKNESS_SERIAL_NUMBER_BYTES> &serial_number);
    robot_common::buffer::ByteRingBuffer _receive_buffer;
    std::size_t _max_payload_size;
    ThicknessQuality _quality;
};
