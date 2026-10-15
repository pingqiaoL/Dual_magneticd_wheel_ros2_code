#include "thickness_usb_driver/ThicknessProtocol.hpp"
#include "ProtocolFixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) { throw std::runtime_error(std::string("check failed: ")+#x); } } while(false)

class ProtocolTest
{
public:
    static void run()
    {
        ByteRingBuffer ring(4);CHECK(ring.push(1));CHECK(ring.push(2));ring.discard(1);
        ring.push(3);ring.push(4);ring.push(5);CHECK(!ring.push(6));CHECK(ring.at(0)==2 && ring.at(3)==5);
        const std::string vector="123456789";
        CHECK(ThicknessProtocol::crc16(reinterpret_cast<const std::uint8_t *>(vector.data()),vector.size())==0x4B37);
        std::array<std::uint8_t,12> request{};const std::string requestSn="EM231108C006";
        std::copy(requestSn.begin(),requestSn.end(),request.begin());
        const auto broadcast=ThicknessProtocol::create_message(MessageType::Broadcast,request);
        const auto heartbeat=ThicknessProtocol::create_message(MessageType::Heartbeat,request);
        CHECK(broadcast==ProtocolFixtures::bytes("FE E1 90 0D 00 01 00 00 FF FF FF 00 00 00 00 00 00 00 00 00 00 0C 45 4D 32 33 31 31 30 38 43 30 30 36 2B 21 0D 90 E1 FE"));
        CHECK(heartbeat==ProtocolFixtures::bytes("FE E1 90 0D 00 01 00 00 FF FF 01 00 00 00 00 00 00 00 00 00 00 0C 45 4D 32 33 31 31 30 38 43 30 30 36 2B 21 0D 90 E1 FE"));
        for(const auto type : {MessageType::CalibrationUpdate, MessageType::CalibrationConfirm,
                               static_cast<MessageType>(255)})
        {
            bool rejected=false;
            try { (void)ThicknessProtocol::create_message(type,request); }
            catch(const std::invalid_argument &) { rejected=true; }
            CHECK(rejected);
        }
        ThicknessProtocol p;std::vector<ThicknessFrame> out;
        const auto discovery=ProtocolFixtures::frame255();
        for(std::size_t i=0;i<discovery.size();++i)
        {p.add_data(&discovery[i],1,out);CHECK(out.size()==(i+1==discovery.size()?1u:0u));}
        CHECK(out.front().category==255 && out.front().payload.size()==25);out.clear();
        auto combined=ProtocolFixtures::frame1();const auto state=ProtocolFixtures::frame2(),measurement=ProtocolFixtures::frame5();
        combined.insert(combined.end(),state.begin(),state.end());combined.insert(combined.end(),measurement.begin(),measurement.end());
        p.add_data(combined.data(),combined.size(),out);CHECK(out.size()==3);
        auto config=ThicknessProtocol::configuration(out[1]);CHECK(config && config->calibration_thickness==1000.0f && config->menu_mode==0);
        auto m=ThicknessProtocol::measurement(out[2]);CHECK(m && m->value==276.287109375f && m->material_raw==1);
        const auto query=ThicknessProtocol::create_message(MessageType::StateQuery,m->device_sn);
        CHECK(query==ProtocolFixtures::bytes("FE E1 90 0D 00 01 00 00 01 03 02 00 00 00 00 00 00 00 00 00 00 0C 43 54 32 36 30 39 31 38 42 30 30 32 CD 25 0D 90 E1 FE"));
        out.clear();auto bad=measurement;bad[35]^=1;
        auto oversized=ProtocolFixtures::frame1();oversized[20]=0xFF;oversized[21]=0xFF;
        std::vector<std::uint8_t> noise{0x23,0xFE,0xFE,0x11};
        noise.insert(noise.end(),bad.begin(),bad.end());noise.insert(noise.end(),oversized.begin(),oversized.end());
        noise.insert(noise.end(),measurement.begin(),measurement.end());p.add_data(noise.data(),noise.size(),out);
        CHECK(out.size()==1 && p.stats().crc_errors>=1 && p.stats().length_errors>=1);
        // 载荷本身含有帧尾字节模式，必须按长度提取，不能搜索第一个帧尾。
        out.clear();auto embedded=measurement;
        embedded[35]=0x0D;embedded[36]=0x90;embedded[37]=0xE1;embedded[38]=0xFE;
        auto crc=ThicknessProtocol::crc16(embedded.data()+22,17);embedded[39]=crc>>8;embedded[40]=crc&0xFF;
        p.add_data(embedded.data(),embedded.size(),out);CHECK(out.size()==1);
        out.clear();auto invalid=measurement;invalid[35]=0x7F;invalid[36]=0xC0;invalid[37]=invalid[38]=0;
        crc=ThicknessProtocol::crc16(invalid.data()+22,17);invalid[39]=crc>>8;invalid[40]=crc&0xFF;
        p.add_data(invalid.data(),invalid.size(),out);CHECK(out.size()==1 && std::isnan(ThicknessProtocol::measurement(out[0])->value));
        out.clear();p.add_data(measurement.data(),10,out);p.clearBuffer();p.add_data(measurement.data(),measurement.size(),out);CHECK(out.size()==1);
        std::cout<<"PASS protocol: unified API/vendor byte vectors, unsupported requests rejected, split/coalesced frames, CRC/length recovery, embedded tail, float/NaN, reconnect reset\n";
    }
};
int main()
{try{ProtocolTest::run();}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
