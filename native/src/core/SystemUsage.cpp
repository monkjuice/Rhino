#include "SystemUsage.h"

#include <algorithm>
#include <chrono>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN 1
 #endif
 #include <windows.h>
 #include <psapi.h>
 #if defined(_MSC_VER)
  #pragma comment(lib, "psapi.lib")
 #endif
#elif defined(__APPLE__)
 #include <mach/mach.h>
 #include <sys/resource.h>
 #include <unistd.h>
#else
 #include <unistd.h>
 #include <cstdio>
 #include <sys/resource.h>
#endif

namespace rhino
{
std::uint64_t SystemUsage::processMemoryBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0)
        return static_cast<std::uint64_t>(counters.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<std::uint64_t>(info.resident_size);
    return 0;
#else
    // /proc reports the resident set in pages; the rusage fallback below is in
    // kilobytes and is a high-water mark rather than a current reading.
    if (auto* file = std::fopen("/proc/self/statm", "r"))
    {
        long long total = 0, resident = 0;
        const auto read = std::fscanf(file, "%lld %lld", &total, &resident);
        std::fclose(file);
        if (read == 2 && resident > 0)
            return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    }
    rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
    return 0;
#endif
}

double SystemUsage::processCpuLoad()
{
    // Both halves of the ratio are sampled here so the reading cannot be skewed
    // by the caller's polling interval drifting.
    static double previousCpuSeconds = 0.0;
    static double previousWallSeconds = 0.0;

    const auto wallSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    double cpuSeconds = 0.0;
#if defined(_WIN32)
    FILETIME creation {}, exited {}, kernel {}, user {};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user) == 0)
        return 0.0;
    const auto toSeconds = [](const FILETIME& time)
    {
        ULARGE_INTEGER value {};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<double>(value.QuadPart) * 1.0e-7; // 100ns ticks
    };
    cpuSeconds = toSeconds(kernel) + toSeconds(user);
    const auto cores = []
    {
        SYSTEM_INFO info {};
        GetSystemInfo(&info);
        return std::max(1, static_cast<int>(info.dwNumberOfProcessors));
    }();
#else
    rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
    cpuSeconds = static_cast<double>(usage.ru_utime.tv_sec) + static_cast<double>(usage.ru_utime.tv_usec) * 1.0e-6
               + static_cast<double>(usage.ru_stime.tv_sec) + static_cast<double>(usage.ru_stime.tv_usec) * 1.0e-6;
    const auto cores = std::max(1, static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN)));
#endif

    const auto wallDelta = wallSeconds - previousWallSeconds;
    const auto cpuDelta = cpuSeconds - previousCpuSeconds;
    const auto hadPrevious = previousWallSeconds > 0.0;
    previousWallSeconds = wallSeconds;
    previousCpuSeconds = cpuSeconds;
    if (!hadPrevious || wallDelta <= 0.0) return 0.0;
    return std::clamp(cpuDelta / (wallDelta * cores), 0.0, 1.0);
}
}
