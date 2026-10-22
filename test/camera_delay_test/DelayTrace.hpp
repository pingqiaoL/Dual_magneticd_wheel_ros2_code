#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

// 仅由 prepare_workspace.py 生成的测试副本包含。
namespace camera_delay_test
{
inline void record(const char *stage, std::uint64_t frame_id, std::uint64_t related_id = 0)
{
    static const int output_fd = []
    {
        const char *directory = std::getenv("CAMERA_DELAY_OUTPUT");
        if (!directory)
            return -1;
        char filename[1024];
        std::snprintf(filename, sizeof(filename), "%s/trace_%d.csv", directory, getpid());
        return ::open(filename, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }();
    if (output_fd < 0)
        return;
    timespec current_time{};
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    const auto timestamp_ns = static_cast<std::uint64_t>(current_time.tv_sec) * 1000000000ULL +
                              current_time.tv_nsec;
    char row[256];
    const int length = std::snprintf(row, sizeof(row), "%llu,%s,%llu,%llu\n",
                                    static_cast<unsigned long long>(timestamp_ns), stage,
                                    static_cast<unsigned long long>(frame_id),
                                    static_cast<unsigned long long>(related_id));
    if (length > 0 && length < static_cast<int>(sizeof(row)))
    {
        const auto written_bytes = ::write(output_fd, row, length);
        (void)written_bytes;
    }
}
} // namespace camera_delay_test
