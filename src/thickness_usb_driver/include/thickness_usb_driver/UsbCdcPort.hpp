#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>

/**
 * USB CDC ACM 字节收发接口，不依赖 ROS，也不解析厂商测厚报文。
 *
 * 构造函数只保存配置，不打开设备。一个对象持有一个文件描述符，
 * 不允许复制或移动。由同一个线程调用打开、读取、关闭等操作；
 * 不支持另一个线程在 readBytes() 等待时关闭或销毁该对象。
 */
class UsbCdcPort final
{
public:
    // 路径和波特率绑定到对象；更换配置时关闭旧对象并创建新对象。
    // CDC 固件是否使用波特率配置，需要另行确认；它不是 USB 总线速率。
    UsbCdcPort(std::string device, unsigned int baudRate);
    ~UsbCdcPort();

    UsbCdcPort(const UsbCdcPort &) = delete;
    UsbCdcPort &operator=(const UsbCdcPort &) = delete;
    UsbCdcPort(UsbCdcPort &&) = delete;
    UsbCdcPort &operator=(UsbCdcPort &&) = delete;

    // 真正打开设备并配置 raw/8N1、无软硬件流控、非阻塞读取。
    // 已打开时直接返回 true，不重复打开。失败后不持有设备描述符。
    bool openPort();

    // 允许重复调用；对象析构时也会执行。不在这里自动重连。
    void closePort() noexcept;

    // 读取一次可用数据，不保证一条完整报文，也不要求填满 buffer。
    // timeoutMs >= 0，单位毫秒；0 表示只检查当前可用数据。
    // >0：实际读取字节数；0：本次等待结束，没有数据；-1：操作失败。
    // 断开或不可恢复的 I/O 错误会关闭设备；参数错误不会关闭设备。
    // poll 的等待受系统调度影响，不是严格的实时截止时间。
    ssize_t readBytes(std::uint8_t *buffer, std::size_t capacity, int timeoutMs);

    // 在同一截止时间内尽量写完。超时可返回0或部分长度；I/O错误返回-1并关闭。
    // 返回成功字节数只表示交给Linux，不代表设备应答。不能跨线程调用。
    ssize_t writeBytes(const std::uint8_t *buffer, std::size_t size, int timeoutMs);

    // 表示当前持有描述符，不是持续的 USB 在线探测；断开由后续 I/O 发现。
    bool isOpen() const noexcept;
    const std::string &devicePath() const noexcept;
    unsigned int baudRate() const noexcept;

    // 最近一次 open/read/write 的错误；成功或正常超时后清空，关闭操作保留它。
    const std::string &lastError() const noexcept;
    int lastErrorCode() const noexcept;

private:
    using Clock = std::chrono::steady_clock;

    // 对剩余时间向上取整，避免 EINTR 重试时重新计算完整等待时长。
    static int remainingTimeoutMs(Clock::time_point deadline) noexcept;
    void clearError() noexcept;
    void setError(const char *operation, int errorCode);

    const std::string _device;
    const unsigned int _baudRate;
    int _fd{-1};
    int _lastErrorCode{0};
    std::string _lastError;
};
