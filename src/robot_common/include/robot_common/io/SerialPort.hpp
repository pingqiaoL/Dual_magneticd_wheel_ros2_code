#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>

namespace robot_common::io
{

/**
 * Linux TTY 字节收发接口，不依赖 ROS，也不解析任何设备协议。
 *
 * 构造函数只保存配置，不打开设备。一个对象持有一个文件描述符，
 * 不允许复制或移动。由同一个线程调用打开、读取、关闭等操作；
 * 不支持另一个线程在 read_bytes() 等待时关闭或销毁该对象。
 */
class SerialPort final
{
  public:
    // 路径和波特率绑定到对象；更换配置时关闭旧对象并创建新对象。
    // CDC 固件是否使用波特率配置，需要另行确认；它不是 USB 总线速率。
    SerialPort(std::string device_path, unsigned int baud_rate);
    ~SerialPort();

    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;
    SerialPort(SerialPort &&) = delete;
    SerialPort &operator=(SerialPort &&) = delete;

    // 真正打开设备并配置 raw/8N1、无软硬件流控、非阻塞读取。
    // 已打开时直接返回 true，不重复打开。失败后不持有设备描述符。
    bool open_port();

    // 允许重复调用；对象析构时也会执行。不在这里自动重连。
    void close_port() noexcept;

    // 读取一次可用数据，不保证一条完整报文，也不要求填满 buffer。
    // timeout_ms >= 0，单位毫秒；0 表示只检查当前可用数据。
    // >0：实际读取字节数；0：本次等待结束，没有数据；-1：操作失败。
    // 断开或不可恢复的 I/O 错误会关闭设备；参数错误不会关闭设备。
    // poll 的等待受系统调度影响，不是严格的实时截止时间。
    ssize_t read_bytes(std::uint8_t *read_buffer, std::size_t buffer_capacity, int timeout_ms);

    // 在同一截止时间内尽量写完。超时可返回0或部分长度；I/O错误返回-1并关闭。
    // 返回成功字节数只表示交给Linux，不代表设备应答。不能跨线程调用。
    ssize_t write_bytes(const std::uint8_t *bytes, std::size_t byte_count, int timeout_ms);

    // 借用当前打开的 Linux 文件描述符，仅用于同一所有者线程执行 poll/ppoll。
    // 调用者不得关闭、复制所有权或修改描述符配置；关闭状态返回 -1。
    int native_handle() const noexcept;

    // 表示当前持有描述符，不是持续的 USB 在线探测；断开由后续 I/O 发现。
    bool is_open() const noexcept;
    static bool is_supported_baud_rate(unsigned int baud_rate) noexcept;
    const std::string &device_path() const noexcept;
    unsigned int baud_rate() const noexcept;

    // 最近一次 open/read/write 的错误；成功或正常超时后清空，关闭操作保留它。
    const std::string &last_error() const noexcept;
    int last_error_code() const noexcept;

  private:
    void clear_error() noexcept;
    void set_error(const char *operation, int error_code);

    const std::string _device_path;
    const unsigned int _baud_rate;
    int _file_descriptor{-1};
    int _last_error_code{0};
    std::string _last_error;
};

} // namespace robot_common::io
