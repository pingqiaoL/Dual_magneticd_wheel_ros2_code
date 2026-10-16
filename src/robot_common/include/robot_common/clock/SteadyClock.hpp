#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

namespace robot_common::clock
{

using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;

// 默认构造的时间点表示“尚未记录”，对应的年龄使用最大值。
inline constexpr std::uint32_t UNKNOWN_ELAPSED_TIME_MS = std::numeric_limits<std::uint32_t>::max();

/**
 * 返回进程内单调递增的时间点。
 *
 * 该时间没有日历含义，只用于超时、周期和耗时计算。
 */
SteadyTimePoint monotonic_time_now() noexcept;

/**
 * 计算两个单调时间点之间的毫秒数。
 *
 * previous_time 尚未记录时返回 UNKNOWN_ELAPSED_TIME_MS；如果时间顺序异常，
 * 返回 0；超过 uint32_t 范围时饱和到最大值。
 */
std::uint32_t elapsed_time_ms(SteadyTimePoint current_time, SteadyTimePoint previous_time) noexcept;

/**
 * 计算从 current_time 到 deadline 的剩余毫秒数。
 *
 * 正值向上取整，避免亚毫秒余量被提前当作超时；已经到期时返回 0。
 */
int remaining_time_ms(SteadyTimePoint deadline, SteadyTimePoint current_time) noexcept;

/**
 * 使用当前单调时间计算截止时间剩余量。
 */
int remaining_time_ms(SteadyTimePoint deadline) noexcept;

} // namespace robot_common::clock
