#include "telemetry_bridge/TelemetryDriver.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace telemetry_bridge;

void check(bool condition, const char *reason)
{
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}

bool wait_until(const std::function<bool()> &predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(2ms);
    }
    return predicate();
}

void read_messages(int descriptor, RemoteProtocol &parser, std::vector<RemoteMessage> &messages)
{
    std::uint8_t bytes[512];
    while (true)
    {
        pollfd event{descriptor, POLLIN, 0};
        if (::poll(&event, 1, 0) <= 0 || (event.revents & POLLIN) == 0)
        {
            return;
        }
        const auto byte_count = ::read(descriptor, bytes, sizeof(bytes));
        if (byte_count <= 0)
        {
            return;
        }
        parser.feed_bytes(bytes, static_cast<std::size_t>(byte_count), messages);
    }
}

bool contains_named_float(const std::vector<RemoteMessage> &messages, const char *name, float value)
{
    for (const auto &message : messages)
    {
        if (message.message_id != 251 || message.payload.size() < 18 ||
            std::memcmp(message.payload.data() + 8, name, std::strlen(name)) != 0)
        {
            continue;
        }
        float received_value = 0;
        std::memcpy(&received_value, message.payload.data() + 4, sizeof(received_value));
        if (received_value == value)
        {
            return true;
        }
    }
    return false;
}

bool contains_named_int(const std::vector<RemoteMessage> &messages, const char *name,
                        std::int32_t value)
{
    for (const auto &message : messages)
    {
        if (message.message_id != 252 || message.payload.size() < 18 ||
            std::memcmp(message.payload.data() + 8, name, std::strlen(name)) != 0)
        {
            continue;
        }
        std::int32_t received_value = 0;
        std::memcpy(&received_value, message.payload.data() + 4, sizeof(received_value));
        if (received_value == value)
        {
            return true;
        }
    }
    return false;
}

int main()
{
    int master_descriptor = -1;
    int slave_descriptor = -1;
    const std::string serial_link =
        "/tmp/telemetry_driver_test_" + std::to_string(static_cast<long long>(::getpid()));
    try
    {
        char device_path[128]{};
        check(::openpty(&master_descriptor, &slave_descriptor, device_path, nullptr, nullptr) == 0,
              "create initial PTY");
        ::close(slave_descriptor);
        slave_descriptor = -1;
        check(::symlink(device_path, serial_link.c_str()) == 0, "create serial symlink");
        check(::fcntl(master_descriptor, F_SETFL, O_NONBLOCK) == 0, "set PTY nonblocking");

        TelemetryConfig config;
        config.device_path = serial_link;
        config.queue_capacity = 4;
        config.reconnect_interval_ms = 20;
        TelemetryDriver driver(config);
        check(!driver.send_named_float("THICKNESS", 1.0F), "reject while stopped");
        driver.start();
        driver.start();
        check(wait_until([&] { return driver.status().port_open; }, 1s), "automatic open");
        check(!driver.send_named_float("THICKNESS", std::numeric_limits<float>::quiet_NaN()),
              "reject non-finite value");
        check(!driver.send_named_float("NAME_TOO_LONG", 1.0F), "reject invalid name");
        check(driver.send_named_float("THICKNESS", 49.25F), "submit named float");
        check(driver.send_named_int("STM_LINK", 0), "submit named int");

        RemoteProtocol parser;
        std::vector<RemoteMessage> messages;
        check(wait_until(
                  [&]
                  {
                      read_messages(master_descriptor, parser, messages);
                      return contains_named_float(messages, "THICKNESS", 49.25F) &&
                             contains_named_int(messages, "STM_LINK", 0);
                  },
                  1s),
              "background thread sends named telemetry");
        check(wait_until(
                  [&]
                  {
                      read_messages(master_descriptor, parser, messages);
                      for (const auto &message : messages)
                      {
                          if (message.message_id == 0)
                          {
                              return true;
                          }
                      }
                      return false;
                  },
                  1s),
              "background heartbeat");
        check(driver.status().written_telemetry == 2, "telemetry write counter");

        ::close(master_descriptor);
        master_descriptor = -1;
        check(wait_until([&] { return !driver.status().port_open; }, 1s),
              "disconnect detected by poll");
        check(!driver.send_named_float("THICKNESS", 50.0F),
              "disconnected driver does not accumulate telemetry");

        check(::openpty(&master_descriptor, &slave_descriptor, device_path, nullptr, nullptr) == 0,
              "create reconnect PTY");
        ::close(slave_descriptor);
        slave_descriptor = -1;
        check(::fcntl(master_descriptor, F_SETFL, O_NONBLOCK) == 0,
              "set reconnect PTY nonblocking");
        check(::unlink(serial_link.c_str()) == 0, "remove old serial symlink");
        check(::symlink(device_path, serial_link.c_str()) == 0, "replace serial symlink");
        check(wait_until([&] { return driver.status().port_open; }, 1s), "automatic reconnect");

        parser.reset();
        messages.clear();
        check(driver.send_named_float("THICKNESS", 52.0F), "submit after reconnect");
        check(wait_until(
                  [&]
                  {
                      read_messages(master_descriptor, parser, messages);
                      return contains_named_float(messages, "THICKNESS", 52.0F);
                  },
                  1s),
              "send after reconnect");

        const auto stop_started_at = std::chrono::steady_clock::now();
        driver.stop();
        check(std::chrono::steady_clock::now() - stop_started_at < 500ms,
              "eventfd wakes stopped driver promptly");
        driver.stop();
        check(!driver.status().is_running && !driver.status().port_open, "idempotent stop");

        driver.start();
        check(wait_until([&] { return driver.status().port_open; }, 1s), "restart driver");
        driver.stop();

        ::close(master_descriptor);
        master_descriptor = -1;
        (void)::unlink(serial_link.c_str());
        std::cout << "PASS autonomous poll loop, eventfd wakeup, named telemetry, heartbeat, "
                     "disconnect, reconnect, prompt stop, restart\n";
    }
    catch (const std::exception &error)
    {
        if (master_descriptor >= 0)
        {
            ::close(master_descriptor);
        }
        if (slave_descriptor >= 0)
        {
            ::close(slave_descriptor);
        }
        (void)::unlink(serial_link.c_str());
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
