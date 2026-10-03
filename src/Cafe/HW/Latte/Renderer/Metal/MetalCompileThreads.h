#pragma once

#include <algorithm>
#include <pthread.h>
#include <sys/sysctl.h>

// Host threads the emulator needs on performance cores while a game is running: 3 guest CPU cores + GPU thread
constexpr uint32 METAL_EMULATION_THREAD_COUNT = 4;
constexpr uint32 METAL_MIN_COMPILE_THREAD_COUNT = 2;
constexpr uint32 METAL_MAX_COMPILE_THREAD_COUNT = 8;

inline uint32 GetSysctlUInt32(const char* name, uint32 fallback)
{
    sint32 value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 || size != sizeof(value) || value <= 0)
        return fallback;

    return (uint32)value;
}

inline uint32 GetPerformanceCoreCount()
{
    static const uint32 s_count = GetSysctlUInt32("hw.perflevel0.physicalcpu", GetSysctlUInt32("hw.physicalcpu", 4));
    return s_count;
}

inline uint32 GetEfficiencyCoreCount()
{
    static const uint32 s_count = GetSysctlUInt32("hw.perflevel1.physicalcpu", 0);
    return s_count;
}

// Performance cores that are left over once every emulation thread has one
inline uint32 GetSparePerformanceCoreCount()
{
    uint32 performanceCoreCount = GetPerformanceCoreCount();
    return performanceCoreCount > METAL_EMULATION_THREAD_COUNT ? performanceCoreCount - METAL_EMULATION_THREAD_COUNT : 0;
}

// Size of the shader and pipeline compile pools used while a game is running
// Spare performance cores plus all efficiency cores, e.g. M1: 0 + 4 -> 4, M1 Max: 4 + 2 -> 6, M3 Max: 8 + 4 -> 8 (capped)
inline uint32 GetCompileThreadCount()
{
    return std::clamp(GetSparePerformanceCoreCount() + GetEfficiencyCoreCount(), METAL_MIN_COMPILE_THREAD_COUNT, METAL_MAX_COMPILE_THREAD_COUNT);
}

// Called by a compile thread on itself
// Threads that fit into the spare performance cores run at USER_INITIATED so that compiles finish quickly (fewer skipped draws and stalls)
// The remaining threads run at UTILITY, which ranks below the emulation threads (default QoS), so they only soak up idle cores
// and never take a performance core away from emulation. The QoS also propagates to MTLCompilerService, which does the actual work
inline void SetCompileThreadQoS(uint32 threadIndex)
{
    qos_class_t qosClass = (threadIndex < GetSparePerformanceCoreCount()) ? QOS_CLASS_USER_INITIATED : QOS_CLASS_UTILITY;
    pthread_set_qos_class_self_np(qosClass, 0);
}
