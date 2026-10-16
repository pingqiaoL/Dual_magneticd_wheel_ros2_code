#include "robot_common/clock/SteadyClock.hpp"
#include "robot_common/math/Saturation.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            throw std::runtime_error(std::string("check failed: ") + #condition);                  \
        }                                                                                          \
    } while (false)

int main()
{
    try
    {
        using robot_common::clock::SteadyTimePoint;

        CHECK(robot_common::math::clamp_to_uint32(-1) == 0);
        CHECK(robot_common::math::clamp_to_uint32(42) == 42);
        CHECK(robot_common::math::clamp_to_uint32(
                  static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1) ==
              std::numeric_limits<std::uint32_t>::max());
        CHECK(robot_common::math::clamp_to_nonnegative_int(-1) == 0);
        CHECK(robot_common::math::clamp_to_nonnegative_int(
                  static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1) ==
              std::numeric_limits<int>::max());

        const SteadyTimePoint previous_time{std::chrono::seconds(10)};
        const SteadyTimePoint current_time = previous_time + std::chrono::milliseconds(1250);
        CHECK(robot_common::clock::elapsed_time_ms(current_time, previous_time) == 1250);
        CHECK(robot_common::clock::elapsed_time_ms(previous_time, current_time) == 0);
        CHECK(robot_common::clock::elapsed_time_ms(current_time, SteadyTimePoint{}) ==
              robot_common::clock::UNKNOWN_ELAPSED_TIME_MS);

        CHECK(robot_common::clock::remaining_time_ms(
                  current_time + std::chrono::microseconds(40001), current_time) == 41);
        CHECK(robot_common::clock::remaining_time_ms(current_time, current_time) == 0);

        const auto monotonic_time = robot_common::clock::monotonic_time_now();
        CHECK(monotonic_time != SteadyTimePoint{});

        std::cout << "PASS robot_common: saturation, monotonic time, elapsed and remaining time\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
