#include "thickness_usb_driver/ThicknessDevice.hpp"
#include "ProtocolFixtures.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <poll.h>
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

        ThicknessDeviceConfig config;
        config.device_path = device_path;
        ThicknessDevice device(config);
        std::vector<DecodedMeasurement> measurements;

        device.process_io(measurements);
        CHECK(measurements.empty());
        std::uint8_t request_bytes[128]{};
        std::size_t request_size = 0;
        while (request_size < 80)
        {
            pollfd descriptor{master_descriptor, POLLIN, 0};
            CHECK(::poll(&descriptor, 1, 100) == 1);
            const ssize_t bytes_read = ::read(master_descriptor, request_bytes + request_size,
                                              sizeof(request_bytes) - request_size);
            CHECK(bytes_read > 0);
            request_size += static_cast<std::size_t>(bytes_read);
        }
        CHECK(request_size == 80);
        CHECK(device.status().port_open);
        CHECK(!device.status().communication_ready);

        auto response_bytes = ProtocolFixtures::heartbeat_response_frame();
        const auto measurement_frame = ProtocolFixtures::measurement_frame();
        response_bytes.insert(response_bytes.end(), measurement_frame.begin(),
                              measurement_frame.end());
        CHECK(::write(master_descriptor, response_bytes.data(), response_bytes.size()) ==
              static_cast<ssize_t>(response_bytes.size()));

        device.process_io(measurements);
        CHECK(measurements.size() == 1);
        CHECK(measurements.front().thickness_value == 276.287109375F);
        const auto online_status = device.status();
        CHECK(online_status.connection_state == ThicknessConnectionState::ONLINE);
        CHECK(online_status.communication_ready);
        CHECK(online_status.quality.valid_frames == 2);

        ::close(master_descriptor);
        device.process_io(measurements);
        const auto disconnected_status = device.status();
        CHECK(disconnected_status.connection_state == ThicknessConnectionState::DISCONNECTED);
        CHECK(!disconnected_status.port_open);
        CHECK(disconnected_status.io_error_count == 1);

        std::cout << "PASS thickness device: open, discovery requests, decode, online state, "
                     "disconnect recovery\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
