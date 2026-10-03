#include <cstdint>
#include <ctime>

uint32_t GetTickCount()
{
	return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000000;

}
