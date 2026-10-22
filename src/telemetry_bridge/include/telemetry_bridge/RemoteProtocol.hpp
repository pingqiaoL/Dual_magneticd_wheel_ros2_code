#pragma once

#include "telemetry_bridge/TelemetryTypes.hpp"

namespace telemetry_bridge
{

// MK32 已有 common 消息子集：发送 MAVLink 2，接收未签名 MAVLink 1/2。
// 不提供车辆控制、测厚校零或未实现命令的成功应答。
class RemoteProtocol final
{
  public:
    static std::vector<std::uint8_t> heartbeat(std::uint8_t sequence);
    static std::vector<std::uint8_t> named_float(std::uint8_t sequence, std::uint32_t time_boot_ms,
                                                 const std::string &name, float value);
    static std::vector<std::uint8_t> named_int(std::uint8_t sequence, std::uint32_t time_boot_ms,
                                               const std::string &name, std::int32_t value);
    static std::vector<std::uint8_t> ping_reply(std::uint8_t sequence,
                                                const RemoteMessage &request);
    static std::vector<std::uint8_t> unsupported_command_ack(std::uint8_t sequence,
                                                             const RemoteMessage &request);
    static void set_sequence(std::vector<std::uint8_t> &frame, std::uint8_t sequence);
    void feed_bytes(const std::uint8_t *bytes, std::size_t byte_count,
                    std::vector<RemoteMessage> &messages);
    void reset();

  private:
    static std::vector<std::uint8_t> build_frame(std::uint8_t sequence, std::uint32_t message_id,
                                                 const std::vector<std::uint8_t> &payload);
    std::vector<std::uint8_t> _receive_buffer;
};

} // namespace telemetry_bridge
