#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

class ProtocolFixtures
{
  public:
    static std::vector<std::uint8_t> parse_hex_bytes(const std::string &hex_text)
    {
        std::istringstream stream(hex_text);
        unsigned int byte_value = 0;
        std::vector<std::uint8_t> parsed_bytes;

        while (stream >> std::hex >> byte_value)
        {
            parsed_bytes.push_back(static_cast<std::uint8_t>(byte_value));
        }

        return parsed_bytes;
    }

    static std::vector<std::uint8_t> discovery_response_frame()
    {
        return parse_hex_bytes("fe e1 90 0d 00 02 00 00 01 03 ff 00 00 00 00 00 00 00 00 00 00 19 "
                               "43 54 32 36 30 39 31 38 42 30 30 32 40 40 04 00 56 31 2e 34 56 31 "
                               "2e 34 00 0c 98 0d 90 e1 fe");
    }

    static std::vector<std::uint8_t> heartbeat_response_frame()
    {
        return parse_hex_bytes("fe e1 90 0d 00 02 00 00 01 03 01 00 00 00 00 00 00 00 00 00 00 0c "
                               "43 54 32 36 30 39 31 38 42 30 30 32 cd 25 0d 90 e1 fe");
    }

    static std::vector<std::uint8_t> state_response_frame()
    {
        return parse_hex_bytes("fe e1 90 0d 00 02 00 00 01 03 02 00 00 00 00 00 00 00 00 00 00 14 "
                               "43 54 32 36 30 39 31 38 42 30 30 32 01 01 00 00 44 7a 00 00 ce 3a "
                               "0d 90 e1 fe");
    }

    static std::vector<std::uint8_t> measurement_frame()
    {
        return parse_hex_bytes("fe e1 90 0d 00 00 00 00 01 03 05 00 00 00 00 00 00 00 00 00 00 11 "
                               "43 54 32 36 30 39 31 38 42 30 30 32 01 43 8a 24 c0 95 61 0d 90 e1 "
                               "fe");
    }
};
