#include "telemetry_bridge/RemoteProtocol.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace telemetry_bridge;

void check(bool condition, const char *reason)
{
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}

int main()
{
    try
    {
        RemoteProtocol protocol;
        auto bytes = RemoteProtocol::named_float(7, 1234, "THICKNESS", 49.25F);
        check(bytes.size() == 30 && bytes[0] == 0xFD && bytes[5] == 1 && bytes[6] == 191,
              "wire header");
        check(bytes[10] == 0xD2 && bytes[11] == 4 && bytes[14] == 0 && bytes[15] == 0 &&
                  bytes[16] == 0x45 && bytes[17] == 0x42,
              "little endian fields");
        check(std::memcmp(bytes.data() + 18, "THICKNESS\0", 10) == 0, "Android name");
        bool rejected_invalid_name = false;
        try
        {
            (void)RemoteProtocol::named_int(0, 0, "BAD\nNAME", 1);
        }
        catch (const std::invalid_argument &)
        {
            rejected_invalid_name = true;
        }
        check(rejected_invalid_name, "reject non-printable MAVLink name");
        std::vector<RemoteMessage> messages;
        for (const auto byte : bytes)
        {
            protocol.feed_bytes(&byte, 1, messages);
        }
        check(messages.size() == 1 && messages[0].message_id == 251, "split frame");
        auto bad_frame = bytes;
        bad_frame.back() ^= 0x20;
        bad_frame.insert(bad_frame.end(), bytes.begin(), bytes.end());
        messages.clear();
        protocol.feed_bytes(bad_frame.data(), bad_frame.size(), messages);
        check(messages.size() == 1, "CRC recovery");

        RemoteMessage ping;
        ping.message_id = 4;
        ping.system_id = 255;
        ping.component_id = 190;
        ping.payload.resize(14);
        ping.payload[0] = 0x45;
        ping.payload[8] = 17;
        auto reply = RemoteProtocol::ping_reply(8, ping);
        messages.clear();
        protocol.feed_bytes(reply.data(), reply.size(), messages);
        check(messages.size() == 1 && messages[0].payload[0] == 0x45 &&
                  messages[0].payload[8] == 17 && messages[0].payload[12] == 255 &&
                  messages[0].payload[13] == 190,
              "PING correlation and destination");
        ping.payload[12] = 2;
        check(RemoteProtocol::ping_reply(0, ping).empty(), "foreign destination ignored");

        RemoteMessage command;
        command.message_id = 76;
        command.system_id = 255;
        command.component_id = 190;
        command.payload.resize(33);
        command.payload[28] = 0x22;
        command.payload[29] = 0x79;
        command.payload[30] = 1;
        command.payload[31] = 191;
        auto ack = RemoteProtocol::unsupported_command_ack(9, command);
        messages.clear();
        protocol.feed_bytes(ack.data(), ack.size(), messages);
        check(messages.size() == 1 && messages[0].payload[2] == 3 && messages[0].payload[8] == 255,
              "unsupported command never accepted");
        RemoteProtocol::set_sequence(bytes, 42);
        messages.clear();
        protocol.feed_bytes(bytes.data(), bytes.size(), messages);
        check(bytes[4] == 42 && messages.size() == 1, "transmit order CRC update");
        std::cout << "PASS MAVLink framing, fields, split input, CRC recovery, PING, ACK\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
