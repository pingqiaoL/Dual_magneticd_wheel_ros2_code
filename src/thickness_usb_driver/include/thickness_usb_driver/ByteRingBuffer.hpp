#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// 单线程所有权。满时拒绝写入，丢弃和重新同步由协议层显式决定。
class ByteRingBuffer final
{
public:
    explicit ByteRingBuffer(std::size_t capacity) : _data(capacity)
    {
        if (capacity == 0) { throw std::invalid_argument("empty byte buffer"); }
    }
    bool push(std::uint8_t byte)
    {
        if (_size == _data.size()) { return false; }
        _data[(_head + _size) % _data.size()] = byte;
        ++_size;
        return true;
    }
    std::uint8_t at(std::size_t offset) const
    {
        if (offset >= _size) { throw std::out_of_range("byte buffer offset"); }
        return _data[(_head + offset) % _data.size()];
    }
    void discard(std::size_t count)
    {
        if (count > _size) { throw std::out_of_range("byte buffer discard"); }
        _head = (_head + count) % _data.size();
        _size -= count;
    }
    void clear() noexcept { _head = 0; _size = 0; }
    std::size_t size() const noexcept { return _size; }
private:
    std::vector<std::uint8_t> _data;
    std::size_t _head{0};
    std::size_t _size{0};
};
