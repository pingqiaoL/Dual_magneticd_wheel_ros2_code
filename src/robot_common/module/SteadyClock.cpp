#include "robot_common/clock/SteadyClock.hpp"

#include "robot_common/math/Saturation.hpp"

namespace robot_common::clock
{

SteadyTimePoint monotonic_time_now() noexcept
{
    return SteadyClock::now();
}

std::uint32_t elapsed_time_ms(SteadyTimePoint current_time, SteadyTimePoint previous_time) noexcept
{
    if (previous_time == SteadyTimePoint{})
    {
        return UNKNOWN_ELAPSED_TIME_MS;
    }

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(current_time - previous_time).count();
    return math::clamp_to_uint32(elapsed_ms);
}

int remaining_time_ms(SteadyTimePoint deadline, SteadyTimePoint current_time) noexcept
{
    const auto remaining_time = deadline - current_time;
    if (remaining_time <= SteadyClock::duration::zero())
    {
        return 0;
    }

    const auto remaining_ms = std::chrono::ceil<std::chrono::milliseconds>(remaining_time).count();
    return math::clamp_to_nonnegative_int(remaining_ms);
}

int remaining_time_ms(SteadyTimePoint deadline) noexcept
{
    return remaining_time_ms(deadline, monotonic_time_now());
}

} // namespace robot_common::clock
