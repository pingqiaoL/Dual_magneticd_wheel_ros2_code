#include "thickness_usb_driver/ThicknessProtocol.hpp"
#include "ProtocolFixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            throw std::runtime_error(std::string("check failed: ") + #condition);                  \
        }                                                                                          \
    } while (false)

class ProtocolTest
{
  public:
    static void run()
    {
        robot_common::buffer::ByteRingBuffer ring_buffer(4);
        CHECK(ring_buffer.push_byte(1));
        CHECK(ring_buffer.push_byte(2));
        ring_buffer.discard_bytes(1);
        CHECK(ring_buffer.push_byte(3));
        CHECK(ring_buffer.push_byte(4));
        CHECK(ring_buffer.push_byte(5));
        CHECK(!ring_buffer.push_byte(6));
        CHECK(ring_buffer.byte_at(0) == 2 && ring_buffer.byte_at(3) == 5);

        const std::string crc_input = "123456789";
        CHECK(ThicknessProtocol::calculate_crc16(
                  reinterpret_cast<const std::uint8_t *>(crc_input.data()), crc_input.size()) ==
              0x4B37);

        std::array<std::uint8_t, 12> request_serial_number{};
        const std::string serial_number_text = "EM231108C006";
        std::copy(serial_number_text.begin(), serial_number_text.end(),
                  request_serial_number.begin());

        const auto broadcast_request =
            ThicknessProtocol::build_request(MessageType::BROADCAST, request_serial_number);
        const auto heartbeat_request =
            ThicknessProtocol::build_request(MessageType::HEARTBEAT, request_serial_number);
        CHECK(broadcast_request ==
              ProtocolFixtures::parse_hex_bytes("FE E1 90 0D 00 01 00 00 FF FF FF 00 00 00 00 00 "
                                                "00 00 00 00 00 0C 45 4D 32 33 31 31 30 38 43 30 "
                                                "30 36 2B 21 0D 90 E1 FE"));
        CHECK(heartbeat_request ==
              ProtocolFixtures::parse_hex_bytes("FE E1 90 0D 00 01 00 00 FF FF 01 00 00 00 00 00 "
                                                "00 00 00 00 00 0C 45 4D 32 33 31 31 30 38 43 30 "
                                                "30 36 2B 21 0D 90 E1 FE"));

        for (const auto message_type :
             {MessageType::CALIBRATION_UPDATE, MessageType::CALIBRATION_CONFIRM,
              static_cast<MessageType>(255)})
        {
            bool is_rejected = false;
            try
            {
                (void)ThicknessProtocol::build_request(message_type, request_serial_number);
            }
            catch (const std::invalid_argument &)
            {
                is_rejected = true;
            }
            CHECK(is_rejected);
        }

        ThicknessProtocol protocol;
        std::vector<ThicknessFrame> decoded_frames;
        const auto discovery_frame = ProtocolFixtures::discovery_response_frame();
        for (std::size_t byte_index = 0; byte_index < discovery_frame.size(); ++byte_index)
        {
            protocol.feed_bytes(&discovery_frame[byte_index], 1, decoded_frames);
            CHECK(decoded_frames.size() == (byte_index + 1 == discovery_frame.size() ? 1U : 0U));
        }
        CHECK(decoded_frames.front().category_code == 255);
        CHECK(decoded_frames.front().payload.size() == 25);

        decoded_frames.clear();
        auto combined_frames = ProtocolFixtures::heartbeat_response_frame();
        const auto state_frame = ProtocolFixtures::state_response_frame();
        const auto measurement_frame = ProtocolFixtures::measurement_frame();
        combined_frames.insert(combined_frames.end(), state_frame.begin(), state_frame.end());
        combined_frames.insert(combined_frames.end(), measurement_frame.begin(),
                               measurement_frame.end());
        protocol.feed_bytes(combined_frames.data(), combined_frames.size(), decoded_frames);
        CHECK(decoded_frames.size() == 3);

        const auto probe_config = ThicknessProtocol::decode_probe_config(decoded_frames[1]);
        CHECK(probe_config && probe_config->calibration_thickness == 1000.0F &&
              probe_config->menu_mode == 0);

        const auto decoded_measurement = ThicknessProtocol::decode_measurement(decoded_frames[2]);
        CHECK(decoded_measurement && decoded_measurement->thickness_value == 276.287109375F &&
              decoded_measurement->raw_material_code == 1);

        const auto state_query_request = ThicknessProtocol::build_request(
            MessageType::STATE_QUERY, decoded_measurement->device_serial_number);
        CHECK(state_query_request ==
              ProtocolFixtures::parse_hex_bytes("FE E1 90 0D 00 01 00 00 01 03 02 00 00 00 00 00 "
                                                "00 00 00 00 00 0C 43 54 32 36 30 39 31 38 42 30 "
                                                "30 32 CD 25 0D 90 E1 FE"));

        decoded_frames.clear();
        auto corrupted_frame = measurement_frame;
        corrupted_frame[35] ^= 1;
        auto oversized_frame = ProtocolFixtures::heartbeat_response_frame();
        oversized_frame[20] = 0xFF;
        oversized_frame[21] = 0xFF;
        std::vector<std::uint8_t> noisy_bytes{0x23, 0xFE, 0xFE, 0x11};
        noisy_bytes.insert(noisy_bytes.end(), corrupted_frame.begin(), corrupted_frame.end());
        noisy_bytes.insert(noisy_bytes.end(), oversized_frame.begin(), oversized_frame.end());
        noisy_bytes.insert(noisy_bytes.end(), measurement_frame.begin(), measurement_frame.end());
        protocol.feed_bytes(noisy_bytes.data(), noisy_bytes.size(), decoded_frames);
        CHECK(decoded_frames.size() == 1);
        CHECK(protocol.quality().crc_errors >= 1);
        CHECK(protocol.quality().payload_length_errors >= 1);

        // 载荷本身含有帧尾字节模式，必须按长度提取，不能搜索第一个帧尾。
        decoded_frames.clear();
        auto embedded_tail_frame = measurement_frame;
        embedded_tail_frame[35] = 0x0D;
        embedded_tail_frame[36] = 0x90;
        embedded_tail_frame[37] = 0xE1;
        embedded_tail_frame[38] = 0xFE;
        auto calculated_crc =
            ThicknessProtocol::calculate_crc16(embedded_tail_frame.data() + 22, 17);
        embedded_tail_frame[39] = static_cast<std::uint8_t>(calculated_crc >> 8);
        embedded_tail_frame[40] = static_cast<std::uint8_t>(calculated_crc & 0xFF);
        protocol.feed_bytes(embedded_tail_frame.data(), embedded_tail_frame.size(), decoded_frames);
        CHECK(decoded_frames.size() == 1);

        decoded_frames.clear();
        auto invalid_float_frame = measurement_frame;
        invalid_float_frame[35] = 0x7F;
        invalid_float_frame[36] = 0xC0;
        invalid_float_frame[37] = 0;
        invalid_float_frame[38] = 0;
        calculated_crc = ThicknessProtocol::calculate_crc16(invalid_float_frame.data() + 22, 17);
        invalid_float_frame[39] = static_cast<std::uint8_t>(calculated_crc >> 8);
        invalid_float_frame[40] = static_cast<std::uint8_t>(calculated_crc & 0xFF);
        protocol.feed_bytes(invalid_float_frame.data(), invalid_float_frame.size(), decoded_frames);
        CHECK(decoded_frames.size() == 1);
        CHECK(
            std::isnan(ThicknessProtocol::decode_measurement(decoded_frames[0])->thickness_value));

        decoded_frames.clear();
        protocol.feed_bytes(measurement_frame.data(), 10, decoded_frames);
        protocol.clear_buffer();
        protocol.feed_bytes(measurement_frame.data(), measurement_frame.size(), decoded_frames);
        CHECK(decoded_frames.size() == 1);

        std::cout << "PASS protocol: unified API/vendor byte vectors, unsupported requests "
                     "rejected, split/coalesced frames, CRC/length recovery, embedded tail, "
                     "float/NaN, reconnect reset\n";
    }
};

int main()
{
    try
    {
        ProtocolTest::run();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
