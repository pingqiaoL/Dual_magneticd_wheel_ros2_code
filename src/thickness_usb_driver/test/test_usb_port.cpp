#include "thickness_usb_driver/UsbCdcPort.hpp"
#include <pty.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if(!(x)) { throw std::runtime_error(std::string("check failed: ")+#x); } } while(false)

int main()
{
    try
    {
        int master=-1,slave=-1;char path[128]{};
        CHECK(::openpty(&master,&slave,path,nullptr,nullptr)==0);::close(slave);
        UsbCdcPort bad(path,12345);CHECK(!bad.openPort());CHECK(!bad.isOpen());
        UsbCdcPort port(path,115200);CHECK(!port.isOpen());
        std::uint8_t input[128]{};CHECK(port.readBytes(input,sizeof(input),0)==-1);
        CHECK(port.openPort());CHECK(port.openPort());
        const std::uint8_t request[]{0xFE,0xE1,0x90,0x0D,0,1,0xFF};
        CHECK(port.writeBytes(request,sizeof(request),100)==sizeof(request));
        CHECK(::read(master,input,sizeof(input))==sizeof(request));CHECK(std::memcmp(input,request,sizeof(request))==0);
        const char *reply="serial-read-test";CHECK(::write(master,reply,std::strlen(reply))>0);
        CHECK(port.readBytes(input,sizeof(input),100)==static_cast<ssize_t>(std::strlen(reply)));
        CHECK(std::memcmp(input,reply,std::strlen(reply))==0);
        CHECK(port.writeBytes(nullptr,1,0)==-1 && port.isOpen());
        CHECK(port.readBytes(input,0,0)==-1 && port.isOpen());
        const auto start=std::chrono::steady_clock::now();
        CHECK(port.readBytes(input,sizeof(input),20)==0);
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
        // 不读取master，制造发送背压，验证部分写入与总截止时间。
        std::vector<std::uint8_t> large(8*1024*1024,0x5A);
        const auto count=port.writeBytes(large.data(),large.size(),20);
        CHECK(count>=0 && count<static_cast<ssize_t>(large.size()));CHECK(port.isOpen());
        CHECK(port.lastErrorCode()==ETIMEDOUT);
        ::close(master);CHECK(port.readBytes(input,sizeof(input),100)==-1);CHECK(!port.isOpen());
        port.closePort();port.closePort();
        std::cout<<"PASS PTY: construction, open, bidirectional bytes, invalid arguments, timeout, partial write, disconnect\n";
    }
    catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
