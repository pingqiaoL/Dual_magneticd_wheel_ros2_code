#include "thickness_usb_driver/UsbCdcPort.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

UsbCdcPort::UsbCdcPort(std::string device, unsigned int baudRate)
    : _device(std::move(device)), _baudRate(baudRate)
{
    // 不做设备 I/O；即使 USB 尚未插入，也可以先创建配置对象。
}

UsbCdcPort::~UsbCdcPort()
{
    closePort();
}

bool UsbCdcPort::openPort()
{
    clearError();

    if (isOpen())
    {
        return true;
    }

    if (_device.empty() || _device.find('\0') != std::string::npos)
    {
        setError("device path is empty or contains NUL", EINVAL);
        return false;
    }

    speed_t speed;
    switch (_baudRate)
    {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:
            setError("unsupported baud rate", EINVAL);
            return false;
    }

    _fd = ::open(_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (_fd < 0)
    {
        setError("open", errno);
        return false;
    }

    termios options{};
    if (::tcgetattr(_fd, &options) < 0)
    {
        const int errorCode = errno;
        closePort();
        setError("tcgetattr", errorCode);
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
        const int errorCode = errno;
        closePort();
        setError("set speed", errorCode);
        return false;
    }

    if (::tcsetattr(_fd, TCSANOW, &options) < 0)
    {
        const int errorCode = errno;
        closePort();
        setError("tcsetattr", errorCode);
        return false;
    }

    // 不 tcflush：不能静默丢掉设备已经上传的数据。报文对齐交给协议层。
    return true;
}

void UsbCdcPort::closePort() noexcept
{
    if (_fd >= 0)
    {
        const int savedErrno = errno;
        const int fd = _fd;
        _fd = -1;
        // Linux 上不因 EINTR 重试 close，以免关闭已被其他代码复用的描述符。
        (void)::close(fd);
        errno = savedErrno;
    }
}

ssize_t UsbCdcPort::readBytes(
    std::uint8_t *buffer, std::size_t capacity, int timeoutMs)
{
    clearError();

    if (!isOpen())
    {
        setError("read on closed device", EBADF);
        return -1;
    }

    if (buffer == nullptr || capacity == 0 || timeoutMs < 0 ||
        capacity > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
    {
        setError("invalid read arguments", EINVAL);
        return -1;
    }

    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    bool firstAttempt = true;

    while (true)
    {
        if (!firstAttempt && Clock::now() >= deadline)
        {
            return 0;
        }
        firstAttempt = false;

        pollfd descriptor{};
        descriptor.fd = _fd;
        descriptor.events = POLLIN;

        const int ready = ::poll(&descriptor, 1, remainingTimeoutMs(deadline));
        if (ready == 0)
        {
            return 0;
        }
        if (ready < 0)
        {
            const int errorCode = errno;
            if (errorCode == EINTR)
            {
                continue;
            }
            closePort();
            setError("poll", errorCode);
            return -1;
        }

        if ((descriptor.revents & POLLNVAL) != 0)
        {
            closePort();
            setError("poll invalid device", EBADF);
            return -1;
        }

        // HUP 与 POLLIN 可能同时出现，先取出仍然可读的尾部数据。
        if ((descriptor.revents & POLLIN) != 0)
        {
            const ssize_t count = ::read(_fd, buffer, capacity);
            if (count > 0)
            {
                return count;
            }
            if (count < 0)
            {
                const int errorCode = errno;
                if (errorCode == EINTR)
                {
                    continue;
                }
                if (errorCode != EAGAIN && errorCode != EWOULDBLOCK)
                {
                    closePort();
                    setError("read", errorCode);
                    return -1;
                }
                // 就绪后数据可能已不可用，继续按同一个截止时间等待。
            }
            else
            {
                closePort();
                setError("device reached end of stream", EIO);
                return -1;
            }
        }

        if ((descriptor.revents & (POLLHUP | POLLERR)) != 0)
        {
            closePort();
            setError("device disconnected or poll I/O error", EIO);
            return -1;
        }
    }
}

bool UsbCdcPort::isOpen() const noexcept
{
    return _fd >= 0;
}

const std::string &UsbCdcPort::devicePath() const noexcept
{
    return _device;
}

unsigned int UsbCdcPort::baudRate() const noexcept
{
    return _baudRate;
}

const std::string &UsbCdcPort::lastError() const noexcept
{
    return _lastError;
}

int UsbCdcPort::lastErrorCode() const noexcept
{
    return _lastErrorCode;
}

int UsbCdcPort::remainingTimeoutMs(Clock::time_point deadline) noexcept
{
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero())
    {
        return 0;
    }
    return static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(remaining).count());
}

void UsbCdcPort::clearError() noexcept
{
    _lastErrorCode = 0;
    _lastError.clear();
}

void UsbCdcPort::setError(const char *operation, int errorCode)
{
    _lastErrorCode = errorCode;
    _lastError = std::string(operation) + ": " + std::strerror(errorCode);
}

ssize_t UsbCdcPort::writeBytes(const std::uint8_t *buffer, std::size_t size, int timeoutMs)
{
    clearError();
    if (!isOpen()) { setError("write on closed device", EBADF); return -1; }
    if (buffer == nullptr || size == 0 || timeoutMs < 0 ||
        size > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
    { setError("invalid write arguments", EINVAL); return -1; }
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::size_t sent = 0;
    bool first = true;
    while (sent < size)
    {
        if (!first && Clock::now() >= deadline)
        { setError("write deadline expired", ETIMEDOUT); return static_cast<ssize_t>(sent); }
        first = false;
        pollfd descriptor{}; descriptor.fd = _fd; descriptor.events = POLLOUT;
        const int ready = ::poll(&descriptor, 1, remainingTimeoutMs(deadline));
        if (ready == 0)
        { setError("write deadline expired", ETIMEDOUT); return static_cast<ssize_t>(sent); }
        if (ready < 0)
        {
            const int code = errno;
            if (code == EINTR) { continue; }
            closePort(); setError("poll write", code); return -1;
        }
        if (descriptor.revents & (POLLNVAL | POLLHUP | POLLERR))
        { closePort(); setError("write device disconnected", EIO); return -1; }
        if (descriptor.revents & POLLOUT)
        {
            const auto count = ::write(_fd, buffer + sent, size - sent);
            if (count > 0) { sent += static_cast<std::size_t>(count); continue; }
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            { continue; }
            const int code = count < 0 ? errno : EIO;
            closePort(); setError("write", code); return -1;
        }
    }
    return static_cast<ssize_t>(sent);
}
