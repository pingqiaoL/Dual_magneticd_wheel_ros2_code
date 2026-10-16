#include "robot_common/io/SerialPort.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <pty.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            throw std::runtime_error(std::string("check failed: ") + #condition);                  \
        }                                                                                          \
    } while (false)

int main()
{
    try
    {
        int master_descriptor = -1;
        int slave_descriptor = -1;
        char device_path[128]{};
        CHECK(::openpty(&master_descriptor, &slave_descriptor, device_path, nullptr, nullptr) == 0);
        ::close(slave_descriptor);

        robot_common::io::SerialPort invalid_port(device_path, 12345);
        CHECK(!invalid_port.open_port());
        CHECK(!invalid_port.is_open());

        robot_common::io::SerialPort serial_port(device_path, 115200);
        CHECK(!serial_port.is_open());
        std::uint8_t read_buffer[128]{};
        CHECK(serial_port.read_bytes(read_buffer, sizeof(read_buffer), 0) == -1);
        CHECK(serial_port.open_port());
        CHECK(serial_port.open_port());

        const std::uint8_t request_bytes[]{0xFE, 0xE1, 0x90, 0x0D, 0, 1, 0xFF};
        CHECK(serial_port.write_bytes(request_bytes, sizeof(request_bytes), 100) ==
              sizeof(request_bytes));
        CHECK(::read(master_descriptor, read_buffer, sizeof(read_buffer)) == sizeof(request_bytes));
        CHECK(std::memcmp(read_buffer, request_bytes, sizeof(request_bytes)) == 0);

        const char *reply_text = "serial-read-test";
        CHECK(::write(master_descriptor, reply_text, std::strlen(reply_text)) > 0);
        CHECK(serial_port.read_bytes(read_buffer, sizeof(read_buffer), 100) ==
              static_cast<ssize_t>(std::strlen(reply_text)));
        CHECK(std::memcmp(read_buffer, reply_text, std::strlen(reply_text)) == 0);

        CHECK(serial_port.write_bytes(nullptr, 1, 0) == -1 && serial_port.is_open());
        CHECK(serial_port.read_bytes(read_buffer, 0, 0) == -1 && serial_port.is_open());

        const auto start_time = std::chrono::steady_clock::now();
        CHECK(serial_port.read_bytes(read_buffer, sizeof(read_buffer), 20) == 0);
        CHECK(std::chrono::steady_clock::now() - start_time < std::chrono::seconds(1));

        // 不读取主端，制造发送背压，验证部分写入与总截止时间。
        std::vector<std::uint8_t> large_payload(8 * 1024 * 1024, 0x5A);
        const auto bytes_written =
            serial_port.write_bytes(large_payload.data(), large_payload.size(), 20);
        CHECK(bytes_written >= 0);
        CHECK(bytes_written < static_cast<ssize_t>(large_payload.size()));
        CHECK(serial_port.is_open());
        CHECK(serial_port.last_error_code() == ETIMEDOUT);

        ::close(master_descriptor);
        CHECK(serial_port.read_bytes(read_buffer, sizeof(read_buffer), 100) == -1);
        CHECK(!serial_port.is_open());
        serial_port.close_port();
        serial_port.close_port();

        std::cout << "PASS PTY: construction, open, bidirectional bytes, invalid arguments, "
                     "timeout, partial write, disconnect\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
