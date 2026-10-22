#pragma once

#include "robot_common/io/SerialPort.hpp"
#include "telemetry_bridge/RemoteProtocol.hpp"

#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace telemetry_bridge
{

// 独占串口的后台驱动；应用只提交遥测数据，不推进底层 I/O。
class TelemetryDriver final
{
  public:
    explicit TelemetryDriver(TelemetryConfig config);
    ~TelemetryDriver();

    TelemetryDriver(const TelemetryDriver &) = delete;
    TelemetryDriver &operator=(const TelemetryDriver &) = delete;
    TelemetryDriver(TelemetryDriver &&) = delete;
    TelemetryDriver &operator=(TelemetryDriver &&) = delete;

    void start();
    void stop();

    bool send_named_float(const std::string &name, float value);
    bool send_named_int(const std::string &name, std::int32_t value);
    TelemetryStatus status() const;

  private:
    using SteadyClock = robot_common::clock::SteadyClock;

    struct DriverEvents
    {
        short serial_events{0};
        bool is_notified{false};
        int error_code{0};
    };

    bool enqueue_telemetry(std::vector<std::uint8_t> bytes);
    bool is_running() const;
    bool has_pending_output() const;
    std::uint32_t time_boot_ms() const;
    int next_wait_timeout_ms() const;
    DriverEvents wait_for_events(bool watch_write, int timeout_ms);
    void notify_worker() noexcept;
    void drain_notifications() noexcept;
    void run();
    void open_if_due();
    void schedule_heartbeat();
    void enqueue_control(std::vector<std::uint8_t> bytes);
    void read_available();
    void flush_pending();
    void handle_disconnect(const std::string &reason);
    void discard_pending();
    void update_queued_frames_locked();

    TelemetryConfig _config;
    robot_common::io::SerialPort _serial_port;
    RemoteProtocol _protocol;
    mutable std::mutex _mutex;
    std::deque<PendingFrame> _telemetry_queue;
    std::deque<PendingFrame> _control_queue;
    std::optional<PendingFrame> _active_frame;
    std::thread _worker;
    int _wakeup_fd{-1};
    bool _is_running{false};
    std::uint8_t _sequence{0};
    TelemetryStatus _status;
    const SteadyClock::time_point _started_at;
    SteadyClock::time_point _next_open_at{};
    SteadyClock::time_point _next_heartbeat_at{};
};

} // namespace telemetry_bridge
