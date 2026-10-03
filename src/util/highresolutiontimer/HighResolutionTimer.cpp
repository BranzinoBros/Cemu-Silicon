#include "util/highresolutiontimer/HighResolutionTimer.h"

HighResolutionTimer HighResolutionTimer::now()
{
	return HighResolutionTimer(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW));
}

HRTick HighResolutionTimer::getFrequency()
{
	return m_freq;
}

uint64 HighResolutionTimer::m_freq = []() -> uint64 {
	return 1000000000;
}();

struct FrameBenchmarkHelper::Entry
{
	HRTick totalTicks{};
	HRTick startTick{};
};

struct
{
	std::map<std::string, FrameBenchmarkHelper::Entry, std::less<>> m_entries;
	uint32 m_frameCount{};
}s_frameBenchmarkHelperState;

FrameBenchmarkHelper::FrameBenchmarkHelper(std::string_view name)
{
	auto& entries = s_frameBenchmarkHelperState.m_entries;
	auto it = entries.find(name);
	if (it == entries.end())
		it = entries.try_emplace(std::string(name)).first;
	m_entry = &it->second;
	m_entry->startTick = HighResolutionTimer::now().getTick();
}

FrameBenchmarkHelper::~FrameBenchmarkHelper()
{
	HRTick endTick = HighResolutionTimer::now().getTick();
	m_entry->totalTicks += (endTick - m_entry->startTick);
}

void FrameBenchmarkHelper::FrameEnd()
{
	if (s_frameBenchmarkHelperState.m_entries.empty())
		return;
	s_frameBenchmarkHelperState.m_frameCount++;
	if (s_frameBenchmarkHelperState.m_frameCount != 100)
		return;
	s_frameBenchmarkHelperState.m_frameCount = 0;
	cemuLog_log(LogType::Force, "FrameBenchmarkHelper results (avg over last 100 frames):");
	for (auto& [name, entry] : s_frameBenchmarkHelperState.m_entries)
	{
		double millisecondsPerFrame = HighResolutionTimer::getTimeDiff(0, entry.totalTicks) * 1000.0 / 100.0;
		entry.totalTicks = 0;
		cemuLog_log(LogType::Force, "  {}: {:.4f} ms/frame", name, millisecondsPerFrame);
	}
}
