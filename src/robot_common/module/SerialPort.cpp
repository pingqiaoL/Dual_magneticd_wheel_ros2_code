#include "robot_common/io/SerialPort.hpp"

#include "robot_common/clock/SteadyClock.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace robot_common::io
{

SerialPort::SerialPort(std::string device_path, unsigned int baud_rate)
    : _device_path(std::move(device_path)), _baud_rate(baud_rate)
{
    // 不做设备 I/O；即使 USB 尚未插入，也可以先创建配置对象。
}

SerialPort::~SerialPort()
{
    close_port();
}

bool SerialPort::open_port()
{
    clear_error();

    if (is_open())
    {
        return true;
    }

    if (_device_path.empty() || _device_path.find('\0') != std::string::npos)
    {
        set_error("device path is empty or contains NUL", EINVAL);
        return false;
    }

    speed_t speed;
    switch (_baud_rate)
    {
        case 9600:
            speed = B9600;
            break;
        case 19200:
            speed = B19200;
            break;
        case 38400:
            speed = B38400;
            break;
        case 57600:
            speed = B57600;
            break;
        case 115200:
            speed = B115200;
            break;
        case 230400:
            speed = B230400;
            break;
        case 460800:
            speed = B460800;
            break;
        case 921600:
            speed = B921600;
            break;
        default:
            set_error("unsupported baud rate", EINVAL);
            return false;
    }

    _file_descriptor = ::open(_device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (_file_descriptor < 0)
    {
        set_error("open", errno);
        return false;
    }

    termios options{};
    if (::tcgetattr(_file_descriptor, &options) < 0)
    {
        const int error_code = errno;
        close_port();
        set_error("tcgetattr", error_code);
        return false;
    }

    ::cfmakeraw(&options);
    options.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    options.c_cflag |= CS8 | CLOCAL | CREAD;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);

    // 等待由 poll 管理；O_NONBLOCK 保证后续 read 不会重新进入阻塞等待。
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;

    if (::cfsetispeed(&options, speed) < 0 || ::cfsetospeed(&options, speed) < 0)
    {
        const int error_code = errno;
        close_port();
        set_error("set speed", error_code);
        return false;
    }

    if (::tcsetattr(_file_descriptor, TCSANOW, &options) < 0)
    {
        const int error_code = errno;
        close_port();
        set_error("tcsetattr", error_code);
        return false;
    }

    // 不 tcflush：不能静默丢掉设备已经上传的数据。报文对齐交给协议层。
    return true;
}

void SerialPort::close_port() noexcept
{
    if (_file_descriptor >= 0)
    {
        const int saved_errno = errno;
        const int file_descriptor = _file_descriptor;
        _file_descriptor = -1;
        // Linux 上不因 EINTR 重试 close，以免关闭已被其他代码复用的描述符。
        (void)::close(file_descriptor);
        errno = saved_errno;
    }
}

ssize_t SerialPort::read_bytes(std::uint8_t *read_buffer, std::size_t buffer_capacity,
                               int timeout_ms)
{
    clear_error();

    if (!is_open())
    {
        set_error("read on closed device", EBADF);
        return -1;
    }

    if (read_buffer == nullptr || buffer_capacity == 0 || timeout_ms < 0 ||
        buffer_capacity > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
    {
        set_error("invalid read arguments", EINVAL);
        return -1;
    }

    const auto deadline =
        robot_common::clock::monotonic_time_now() + std::chrono::milliseconds(timeout_ms);
    bool is_first_attempt = true;

    while (true)
    {
        if (!is_first_attempt && robot_common::clock::monotonic_time_now() >= deadline)
        {
            return 0;
        }
        is_first_attempt = false;

        pollfd descriptor{};
        descriptor.fd = _file_descriptor;
        descriptor.events = POLLIN;

        const int poll_result =
            ::poll(&descriptor, 1, robot_common::clock::remaining_time_ms(deadline));
        if (poll_result == 0)
        {
            return 0;
        }
        if (poll_result < 0)
        {
            const int error_code = errno;
            if (error_code == EINTR)
            {
                continue;
            }
            close_port();
            set_error("poll", error_code);
            return -1;
        }

        if ((descriptor.revents & POLLNVAL) != 0)
        {
            close_port();
            set_error("poll invalid device", EBADF);
            return -1;
        }

        // HUP 与 POLLIN 可能同时出现，先取出仍然可读的尾部数据。
        if ((descriptor.revents & POLLIN) != 0)
        {
            const ssize_t bytes_read = ::read(_file_descriptor, read_buffer, buffer_capacity);
            if (bytes_read > 0)
            {
                return bytes_read;
            }
            if (bytes_read < 0)
            {
                const int error_code = errno;
                if (error_code == EINTR)
                {
                    continue;
                }
                if (error_code != EAGAIN && error_code != EWOULDBLOCK)
                {
                    close_port();
                    set_error("read", error_code);
                    return -1;
                }
                // 就绪后数据可能已不可用，继续按同一个截止时间等待。
            }
            else
            {
                close_port();
                set_error("device reached end of stream", EIO);
                return -1;
            }
        }

        if ((descriptor.revents & (POLLHUP | POLLERR)) != 0)
        {
            close_port();
            set_error("device disconnected or poll I/O error", EIO);
            return -1;
        }
    }
}

bool SerialPort::is_open() const noexcept
{
    return _file_descriptor >= 0;
}

int SerialPort::native_handle() const noexcept
{
    return _file_descriptor;
}

bool SerialPort::is_supported_baud_rate(unsigned int baud_rate) noexcept
{
    switch (baud_rate)
    {
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
        case 230400:
        case 460800:
        case 921600:
            return true;
        default:
            return false;
    }
}

const std::string &SerialPort::device_path() const noexcept
{
    return _device_path;
}

unsigned int SerialPort::baud_rate() const noexcept
{
    return _baud_rate;
}

const std::string &SerialPort::last_error() const noexcept
{
    return _last_error;
}

int SerialPort::last_error_code() const noexcept
{
    return _last_error_code;
}

void SerialPort::clear_error() noexcept
{
    _last_error_code = 0;
    _last_error.clear();
}

void SerialPort::set_error(const char *operation, int error_code)
{
    _last_error_code = error_code;
    _last_error = std::string(operation) + ": " + std::strerror(error_code);
}

ssize_t SerialPort::write_bytes(const std::uint8_t *bytes, std::size_t byte_count, int timeout_ms)
{
    clear_error();
    if (!is_open())
    {
        set_error("write on closed device", EBADF);
        return -1;
    }
    if (bytes == nullptr || byte_count == 0 || timeout_ms < 0 ||
        byte_count > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
    {
        set_error("invalid write arguments", EINVAL);
        return -1;
    }

    const auto deadline =
        robot_common::clock::monotonic_time_now() + std::chrono::milliseconds(timeout_ms);
    std::size_t bytes_sent = 0;
    bool is_first_attempt = true;
    while (bytes_sent < byte_count)
    {
        if (!is_first_attempt && robot_common::clock::monotonic_time_now() >= deadline)
        {
            set_error("write deadline expired", ETIMEDOUT);
            return static_cast<ssize_t>(bytes_sent);
        }
        is_first_attempt = false;

        pollfd descriptor{};
        descriptor.fd = _file_descriptor;
        descriptor.events = POLLOUT;
        const int poll_result =
            ::poll(&descriptor, 1, robot_common::clock::remaining_time_ms(deadline));
        if (poll_result == 0)
        {
            set_error("write deadline expired", ETIMEDOUT);
            return static_cast<ssize_t>(bytes_sent);
        }
        if (poll_result < 0)
        {
            const int error_code = errno;
            if (error_code == EINTR)
            {
                continue;
            }
            close_port();
            set_error("poll write", error_code);
            return -1;
        }
        if ((descriptor.revents & (POLLNVAL | POLLHUP | POLLERR)) != 0)
        {
            close_port();
            set_error("write device disconnected", EIO);
            return -1;
        }
        if ((descriptor.revents & POLLOUT) != 0)
        {
            const ssize_t bytes_written =
                ::write(_file_descriptor, bytes + bytes_sent, byte_count - bytes_sent);
            if (bytes_written > 0)
            {
                bytes_sent += static_cast<std::size_t>(bytes_written);
                continue;
            }
            if (bytes_written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            const int error_code = bytes_written < 0 ? errno : EIO;
            close_port();
            set_error("write", error_code);
            return -1;
        }
    }
    return static_cast<ssize_t>(bytes_sent);
}

} // namespace robot_common::io
