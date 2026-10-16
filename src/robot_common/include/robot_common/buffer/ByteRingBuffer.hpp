#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace robot_common::buffer
{

// 单线程所有权。满时拒绝写入，丢弃和重新同步由协议层显式决定。
class ByteRingBuffer final
{
  public:
    explicit ByteRingBuffer(std::size_t buffer_capacity) : _storage(buffer_capacity)
    {
        if (buffer_capacity == 0)
        {
            throw std::invalid_argument("empty byte buffer");
        }
    }

    bool push_byte(std::uint8_t byte)
    {
        if (_stored_size == _storage.size())
        {
            return false;
        }
        _storage[(_head_index + _stored_size) % _storage.size()] = byte;
        ++_stored_size;
        return true;
    }

    std::uint8_t byte_at(std::size_t byte_offset) const
    {
        if (byte_offset >= _stored_size)
        {
            throw std::out_of_range("byte buffer offset");
        }
        return _storage[(_head_index + byte_offset) % _storage.size()];
    }

    void discard_bytes(std::size_t byte_count)
    {
        if (byte_count > _stored_size)
        {
            throw std::out_of_range("byte buffer discard");
        }
        _head_index = (_head_index + byte_count) % _storage.size();
        _stored_size -= byte_count;
    }

    void clear() noexcept
    {
        _head_index = 0;
        _stored_size = 0;
    }

    std::size_t size() const noexcept
    {
        return _stored_size;
    }

  private:
    std::vector<std::uint8_t> _storage;
    std::size_t _head_index{0};
    std::size_t _stored_size{0};
};

} // namespace robot_common::buffer
