#pragma once

#include "thickness_usb_driver/ByteRingBuffer.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// 完整协议帧，尚未解释为测量值或仪器状态。
struct ThicknessFrame
{
    std::uint8_t function{};
    std::uint8_t company{};
    std::uint8_t product{};
    std::uint8_t category{};
    std::uint16_t transaction{};
    std::uint16_t status{};
    std::vector<std::uint8_t> payload;
};

struct DecodedMeasurement
{
    std::array<std::uint8_t, 12> device_sn{};
    float value{};
    std::uint8_t material_raw{};
};

struct DecodedProbeConfiguration
{
    std::array<std::uint8_t, 12> device_sn{};
    std::uint8_t current_probe{};
    std::uint8_t calibration_probe{};
    std::uint8_t menu_mode{};
    std::uint8_t device_probe{};
    float calibration_thickness{};
};

// 通信质量统计。
struct Thickness_quality
{
    std::uint64_t valid_frames{};
    std::uint64_t crc_errors{};
    std::uint64_t framing_errors{};
    std::uint64_t length_errors{};
    std::uint64_t buffer_overflows{};
    std::uint64_t discarded_bytes{};
};

enum class MessageType
{
    Broadcast,
    Heartbeat,
    StateQuery,
    CalibrationUpdate,  // 预留：当前不支持，create_message会明确抛出异常。
    CalibrationConfirm, // 预留：当前不支持，create_message会明确抛出异常。
    TemperatureQuery    // 候选类别0x06，仅构帧；未实机验证，采集节点不调用。
};

// 普通C++协议库，不依赖ROS，不打开设备、不发送校准或更新命令。
class ThicknessProtocol final
{
  public:
    explicit ThicknessProtocol(std::size_t capacity = 4096, std::size_t maxPayload = 128);
    void add_data(const std::uint8_t *bytes, std::size_t count,
                  std::vector<ThicknessFrame> &frames);
    void clearBuffer() noexcept
    {
        _buffer.clear();
    }
    const Thickness_quality &stats() const noexcept
    {
        return _stats;
    }
    static std::uint16_t crc16(const std::uint8_t *bytes, std::size_t count);
    // 广播/心跳使用厂家请求SN；已发现设备的状态查询使用实际业务SN。
    static std::vector<std::uint8_t> create_message(MessageType type,
                                                    const std::array<std::uint8_t, 12> &serial);

    static bool validSerial(const std::uint8_t *bytes);
    static std::optional<DecodedMeasurement> measurement(const ThicknessFrame &frame);
    static std::optional<DecodedProbeConfiguration> configuration(const ThicknessFrame &frame);

  private:
    bool extract(std::vector<ThicknessFrame> &frames);
    static float analyze_oridata(const std::uint8_t *bytes);
    // 仅生成“功能01 + 12字节SN载荷”的请求，不能用于携带校准数据的写入。
    static std::vector<std::uint8_t>
    create_general_message(std::uint8_t company, std::uint8_t product, std::uint8_t category,
                           const std::array<std::uint8_t, 12> &serial);
    ByteRingBuffer _buffer;
    std::size_t _maxPayload;
    Thickness_quality _stats;
};
