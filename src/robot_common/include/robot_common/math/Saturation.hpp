#pragma once

#include <cstdint>
#include <limits>

namespace robot_common::math
{

/**
 * 将有符号整数限制在 uint32_t 的有效范围内。
 *
 * 负数返回 0，超过上限的值返回 UINT32_MAX。
 */
constexpr std::uint32_t clamp_to_uint32(std::int64_t value) noexcept
{
    if (value <= 0)
    {
        return 0;
    }

    constexpr auto UINT32_MAX_AS_INT64 =
        static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
    if (value >= UINT32_MAX_AS_INT64)
    {
        return std::numeric_limits<std::uint32_t>::max();
    }

    return static_cast<std::uint32_t>(value);
}

/**
 * 将有符号整数限制在非负 int 的有效范围内。
 */
constexpr int clamp_to_nonnegative_int(std::int64_t value) noexcept
{
    if (value <= 0)
    {
        return 0;
    }

    if (value >= std::numeric_limits<int>::max())
    {
        return std::numeric_limits<int>::max();
    }

    return static_cast<int>(value);
}

} // namespace robot_common::math
