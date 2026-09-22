#pragma once

#include <cstdint>

namespace rhino
{
// Process-wide resource readings for the control bar. Both are cheap enough to
// poll on the UI timer, but neither is free: the Windows calls walk kernel
// structures, so sample them a few times a second, never per frame.
struct SystemUsage
{
    // Resident set: the physical memory this process is holding right now.
    // Zero when the platform refuses to answer, which the caller treats as
    // "nothing to show" rather than as "no memory in use".
    static std::uint64_t processMemoryBytes();

    // Share of one core this process has used since the previous call, 0..1
    // scaled by the number of cores so a fully busy machine reads 1. The first
    // call has no interval behind it and answers 0.
    static double processCpuLoad();
};
}
