#include "telemetry_bridge/TelemetryDriver.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace telemetry_bridge
{

TelemetryDriver::TelemetryDriver(TelemetryConfig config)
    : _config(std::move(config)), _serial_port(_config.device_path, _config.baud_rate),
      _started_at(robot_common::clock::monotonic_time_now())
{
    if (_config.device_path.empty() || _config.device_path.front() != '/' ||
        _config.device_path.find('\0') != std::string::npos ||
        !robot_common::io::SerialPort::is_supported_baud_rate(_config.baud_rate) ||
        _config.queue_capacity == 0 || _config.telemetry_max_age_ms == 0 ||
        _config.reconnect_interval_ms == 0)
    {
        throw std::invalid_argument("invalid telemetry serial config");
    }
    _wakeup_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (_wakeup_fd < 0)
    {
        throw std::system_error(errno, std::generic_category(), "eventfd");
    }
}

TelemetryDriver::~TelemetryDriver()
{
    stop();
    if (_wakeup_fd >= 0)
    {
        (void)::close(_wakeup_fd);
        _wakeup_fd = -1;
    }
}

void TelemetryDriver::start()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_is_running)
    {
        return;
    }
    _is_running = true;
    _status.is_running = true;
    _status.last_error.clear();
    _worker = std::thread(&TelemetryDriver::run, this); //单线程创建
}

void TelemetryDriver::stop()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_running)
        {
            return;
        }
        _is_running = false;
        _status.is_running = false;
    }
    notify_worker();
    if (_worker.joinable())
    {
        _worker.join();
    }
}

bool TelemetryDriver::send_named_float(const std::string &name, float value)
{
    if (!std::isfinite(value))
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_status.discarded_telemetry;
        return false;
    }
    try
    {
        return enqueue_telemetry(RemoteProtocol::named_float(0, time_boot_ms(), name, value));
    }
    catch (const std::invalid_argument &)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_status.discarded_telemetry;
        return false;
    }
}

bool TelemetryDriver::send_named_int(const std::string &name, std::int32_t value)
{
    try
    {
        return enqueue_telemetry(RemoteProtocol::named_int(0, time_boot_ms(), name, value));
    }
    catch (const std::invalid_argument &)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_status.discarded_telemetry;
        return false;
    }
}

bool TelemetryDriver::enqueue_telemetry(std::vector<std::uint8_t> bytes)
{
    {
        std::lock_guard<std::mutex> lock(_mutex); //加锁
        if (!_is_running || !_status.port_open)
        {
            ++_status.discarded_telemetry;
            return false;
        }
        if (_telemetry_queue.size() >= _config.queue_capacity)
        {
            _telemetry_queue.pop_front();
            ++_status.discarded_telemetry;
        }
        _telemetry_queue.push_back(
            {std::move(bytes), 0, true, robot_common::clock::monotonic_time_now()});
        update_queued_frames_locked();
    }
    notify_worker();
    return true;
}

bool TelemetryDriver::is_running() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _is_running;
}

//是否有写数据
bool TelemetryDriver::has_pending_output() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _active_frame.has_value() || !_control_queue.empty() || !_telemetry_queue.empty();
}

std::uint32_t TelemetryDriver::time_boot_ms() const
{
    const auto elapsed = robot_common::clock::monotonic_time_now() - _started_at;
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

int TelemetryDriver::next_wait_timeout_ms() const
{
    const auto current_time = robot_common::clock::monotonic_time_now();
    auto deadline = _serial_port.is_open() ? _next_heartbeat_at : _next_open_at;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto choose_telemetry_deadline = [&](const PendingFrame &frame)
        {
            if (frame.is_telemetry && frame.write_offset == 0)
            {
                const auto expiry =
                    frame.queued_at + std::chrono::milliseconds(_config.telemetry_max_age_ms);
                if (deadline == SteadyClock::time_point{} || expiry < deadline)
                {
                    deadline = expiry;
                }
            }
        };
        if (_active_frame)
        {
            choose_telemetry_deadline(*_active_frame);
        }
        if (!_telemetry_queue.empty())
        {
            choose_telemetry_deadline(_telemetry_queue.front());
        }
    }
    if (deadline == SteadyClock::time_point{} || deadline <= current_time)
    {
        return 0;
    }
    return robot_common::clock::remaining_time_ms(deadline);
}

TelemetryDriver::DriverEvents TelemetryDriver::wait_for_events(bool watch_write, int timeout_ms)
{
    const auto deadline =
        robot_common::clock::monotonic_time_now() + std::chrono::milliseconds(timeout_ms);
    while (true)
    {
        pollfd descriptors[2]{};                          //两个唤醒fd
        descriptors[0].fd = _serial_port.native_handle(); //串口文件描述符
        descriptors[0].events =
            POLLIN | (watch_write ? POLLOUT : 0); //监视触发事件，revent返回实际事件
        descriptors[1].fd = _wakeup_fd;
        descriptors[1].events = POLLIN;

        const int poll_result =
            ::poll(descriptors, 2, robot_common::clock::remaining_time_ms(deadline)); //阻塞
        if (poll_result > 0)
        {
            const bool has_wakeup_error =
                (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            return DriverEvents{descriptors[0].revents, (descriptors[1].revents & POLLIN) != 0,
                                has_wakeup_error ? EIO : 0};
        }
        if (poll_result == 0)
        {
            return DriverEvents{};
        }
        if (errno != EINTR)
        {
            return DriverEvents{0, false, errno};
        }
        if (robot_common::clock::monotonic_time_now() >= deadline)
        {
            return DriverEvents{};
        }
    }
}

void TelemetryDriver::notify_worker() noexcept
{
    const std::uint64_t notification = 1;
    while (::write(_wakeup_fd, &notification, sizeof(notification)) < 0 && errno == EINTR)
    {
    }
}

void TelemetryDriver::drain_notifications() noexcept
{
    std::uint64_t notification = 0;
    while (::read(_wakeup_fd, &notification, sizeof(notification)) < 0 && errno == EINTR)
    {
    }
}

void TelemetryDriver::run()
{
    _sequence = 0;
    _next_open_at = {};
    _next_heartbeat_at = {};
    while (is_running())
    {
        open_if_due();
        if (_serial_port.is_open())
        {
            schedule_heartbeat();
        }

        const auto events = wait_for_events(has_pending_output(), next_wait_timeout_ms()); //阻塞
        if (events.is_notified)
        {
            drain_notifications();
        }
        if (!is_running())
        {
            break;
        }
        if (events.error_code != 0)
        {
            handle_disconnect(std::string("event wait: ") + std::strerror(events.error_code));
            continue;
        }
        if ((events.serial_events & POLLIN) != 0)
        {
            read_available();
        }
        if (_serial_port.is_open() && (events.serial_events & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
            handle_disconnect("serial poll reported disconnect");
            continue;
        }
        if (_serial_port.is_open() && (events.serial_events & POLLOUT) != 0)
        {
            flush_pending();
        }
    }
    _serial_port.close_port();
    discard_pending();
    std::lock_guard<std::mutex> lock(_mutex);
    _status.port_open = false;
}

void TelemetryDriver::open_if_due()
{
    if (_serial_port.is_open() || robot_common::clock::monotonic_time_now() < _next_open_at)
    {
        return;
    }
    if (!_serial_port.open_port())
    {
        handle_disconnect(_serial_port.last_error());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status.port_open = true;
        _status.last_error.clear();
    }
    _next_heartbeat_at = {};
}

void TelemetryDriver::schedule_heartbeat()
{
    const auto current_time = robot_common::clock::monotonic_time_now();
    if (current_time < _next_heartbeat_at)
    {
        return;
    }
    _next_heartbeat_at = current_time + std::chrono::seconds(1);
    enqueue_control(RemoteProtocol::heartbeat(0));
}

void TelemetryDriver::enqueue_control(std::vector<std::uint8_t> bytes)
{
    if (bytes.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    const auto is_same_heartbeat = [&bytes](const PendingFrame &frame)
    { return frame.bytes[7] == bytes[7] && bytes[7] == 0; };
    _control_queue.erase(
        std::remove_if(_control_queue.begin(), _control_queue.end(), is_same_heartbeat),
        _control_queue.end());
    if (_active_frame && _active_frame->write_offset == 0 && is_same_heartbeat(*_active_frame))
    {
        _active_frame.reset();
    }
    if (_control_queue.size() >= 32)
    {
        _control_queue.pop_front();
    }
    _control_queue.push_back(
        {std::move(bytes), 0, false, robot_common::clock::monotonic_time_now()});
    update_queued_frames_locked();
}

void TelemetryDriver::read_available()
{
    std::uint8_t read_buffer[256];
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto bytes_read = _serial_port.read_bytes(read_buffer, sizeof(read_buffer), 0);
        if (bytes_read < 0)
        {
            handle_disconnect(_serial_port.last_error());
            return;
        }
        if (bytes_read == 0)
        {
            return;
        }
        std::vector<RemoteMessage> messages;
        _protocol.feed_bytes(read_buffer, static_cast<std::size_t>(bytes_read), messages);
        for (const auto &message : messages)
        {
            if (message.system_id != 255 || message.component_id != 190)
            {
                continue;
            }
            if (message.message_id == 4)
            {
                enqueue_control(RemoteProtocol::ping_reply(0, message));
            }
            else if (message.message_id == 76)
            {
                enqueue_control(RemoteProtocol::unsupported_command_ack(0, message));
            }
        }
    }
}

void TelemetryDriver::flush_pending()
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        bool write_failed = false;
        std::string write_error;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_active_frame)
            {
                if (!_control_queue.empty())
                {
                    _active_frame = std::move(_control_queue.front());
                    _control_queue.pop_front();
                }
                else if (!_telemetry_queue.empty())
                {
                    _active_frame = std::move(_telemetry_queue.front());
                    _telemetry_queue.pop_front();
                }
                else
                {
                    update_queued_frames_locked();
                    return;
                }
                RemoteProtocol::set_sequence(_active_frame->bytes, _sequence++);
                update_queued_frames_locked();
            }
            const auto age_ms = robot_common::clock::elapsed_time_ms(
                robot_common::clock::monotonic_time_now(), _active_frame->queued_at);
            if (_active_frame->is_telemetry && _active_frame->write_offset == 0 &&
                age_ms > _config.telemetry_max_age_ms)
            {
                ++_status.discarded_telemetry;
                _active_frame.reset();
                update_queued_frames_locked();
                continue;
            }
            auto &frame = *_active_frame;
            //此处不进行阻塞操作，阻塞操作在poll进行
            const auto bytes_written =
                _serial_port.write_bytes(frame.bytes.data() + frame.write_offset,
                                         frame.bytes.size() - frame.write_offset, 0);
            if (bytes_written < 0)
            {
                write_failed = true;
                write_error = _serial_port.last_error();
            }
            else
            {
                frame.write_offset += static_cast<std::size_t>(bytes_written);
                if (frame.write_offset != frame.bytes.size())
                {
                    return;
                }
                if (frame.is_telemetry)
                {
                    ++_status.written_telemetry;
                }
                _active_frame.reset();
                update_queued_frames_locked();
            }
        }
        if (write_failed)
        {
            handle_disconnect(write_error);
            return;
        }
    }
}

void TelemetryDriver::handle_disconnect(const std::string &reason)
{
    _serial_port.close_port();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_status.io_errors;
        _status.last_error = reason;
        _status.port_open = false;
    }
    discard_pending();
    _protocol.reset();
    _next_open_at = robot_common::clock::monotonic_time_now() +
                    std::chrono::milliseconds(_config.reconnect_interval_ms);
}

void TelemetryDriver::discard_pending()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _status.discarded_telemetry += _telemetry_queue.size();
    _telemetry_queue.clear();
    if (_active_frame && _active_frame->is_telemetry)
    {
        ++_status.discarded_telemetry;
    }
    _active_frame.reset();
    _control_queue.clear();
    update_queued_frames_locked();
}

void TelemetryDriver::update_queued_frames_locked()
{
    _status.queued_frames =
        _telemetry_queue.size() + _control_queue.size() + (_active_frame ? 1 : 0);
}

TelemetryStatus TelemetryDriver::status() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _status;
}

} // namespace telemetry_bridge
