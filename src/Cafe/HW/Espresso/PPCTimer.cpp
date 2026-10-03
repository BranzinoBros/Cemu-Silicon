#include "Cafe/HW/Espresso/Const.h"
#include "config/ActiveSettings.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"
#include "Common/cpu_features.h"

#include <numeric>

#if defined(ARCH_X86_64)
#include <immintrin.h>
#pragma intrinsic(__rdtsc)
#endif

#if defined(__aarch64__) && BOOST_OS_MACOS
#include <mach/mach_time.h>
#endif

uint64 _rdtscFrequency = 0;

struct uint128_t
{
	uint64 low;
	uint64 high;
};

static_assert(sizeof(uint128_t) == 16);

// The guest timebase is a pure function of the host counter within an epoch:
//   guestTick = guestBase + ((((hostNow - hostBase) * CORE_CLOCK) / hostFrequency) << 3) >> shiftFactor
// A new epoch is only created when the timer is (re)started or when the timer speed setting changes.
// Epochs are immutable once published and never freed, so readers need no lock
struct PPCTimerEpoch
{
	uint64 hostBase;
	uint64 guestBase;
	uint64 multiplier; // CORE_CLOCK / gcd(CORE_CLOCK, hostFrequency)
	uint64 divisor; // hostFrequency / gcd(CORE_CLOCK, hostFrequency)
	uint8 shiftFactor;
};

static std::atomic<const PPCTimerEpoch*> s_timerEpoch{nullptr};
static std::atomic<uint64> s_lastGuestTick{0}; // highest tick value handed out so far, guarantees monotonicity across threads
static std::mutex s_timerEpochMutex;
static std::vector<std::unique_ptr<PPCTimerEpoch>> s_timerEpochStorage;

// read host counter, ordered after all preceding instructions
static inline uint64 PPCTimer_readHostCounter()
{
#if defined(__aarch64__)
	uint64 t;
	asm volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(t) :: "memory");
	return t;
#else
	_mm_mfence();
	return __rdtsc();
#endif
}

static uint64 PPCTimer_epochToGuestTick(const PPCTimerEpoch* epoch, uint64 hostNow)
{
	uint64 hostDiff = hostNow - epoch->hostBase;
	// a counter value older than the epoch start counts as zero elapsed time
	hostDiff = hostDiff & ~(uint64)((sint64)hostDiff >> 63);

	uint128_t product{};
	product.low = _umul128(hostDiff, epoch->multiplier, &product.high);
	uint64 elapsedTick;
	if (product.high == 0)
	{
		elapsedTick = product.low / epoch->divisor;
	}
	else
	{
		uint64 remainder;
		elapsedTick = _udiv128(product.high, product.low, epoch->divisor, &remainder);
	}

	// timer scaling
	elapsedTick <<= 3ull; // *8
	elapsedTick >>= epoch->shiftFactor;
	return epoch->guestBase + elapsedTick;
}

// caller must hold s_timerEpochMutex
static const PPCTimerEpoch* PPCTimer_publishEpoch(uint64 hostBase, uint64 guestBase, uint8 shiftFactor)
{
	const uint64 hostFrequency = _rdtscFrequency;
	cemu_assert(hostFrequency != 0);
	const uint64 divider = std::gcd(Espresso::CORE_CLOCK, hostFrequency);
	auto epoch = std::make_unique<PPCTimerEpoch>();
	epoch->hostBase = hostBase;
	epoch->guestBase = guestBase;
	epoch->multiplier = Espresso::CORE_CLOCK / divider;
	epoch->divisor = hostFrequency / divider;
	epoch->shiftFactor = shiftFactor;
	const PPCTimerEpoch* epochPtr = epoch.get();
	s_timerEpochStorage.emplace_back(std::move(epoch));
	s_timerEpoch.store(epochPtr, std::memory_order_release);
	return epochPtr;
}

#if defined(__aarch64__)

static uint64 PPCTimer_getHostCounterFrequency()
{
	uint64 frequency;
	asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
#if BOOST_OS_MACOS
	if (frequency == 0)
	{
		// cntvct_el0 is the counter backing mach_absolute_time
		mach_timebase_info_data_t timebase;
		if (mach_timebase_info(&timebase) == KERN_SUCCESS && timebase.numer != 0)
			frequency = (1000000000ULL * timebase.denom) / timebase.numer;
	}
#endif
	return frequency;
}

void PPCTimer_init()
{
	// the generic timer frequency is fixed and reported by the hardware, no calibration needed
	const uint64 hostCounterStart = PPCTimer_readHostCounter();
	_rdtscFrequency = PPCTimer_getHostCounterFrequency();
	cemu_assert(_rdtscFrequency != 0);
	std::unique_lock _l(s_timerEpochMutex);
	PPCTimer_publishEpoch(hostCounterStart, 0, ActiveSettings::GetTimerShiftFactor());
}

#else

uint64 muldiv64(uint64 a, uint64 b, uint64 d)
{
	uint64 diva = a / d;
	uint64 moda = a % d;
	uint64 divb = b / d;
	uint64 modb = b % d;
	return diva * b + moda * divb + moda * modb / d;
}

uint64 PPCTimer_estimateRDTSCFrequency()
{
    #if defined(ARCH_X86_64)
	if (!g_CPUFeatures.x86.invariant_tsc)
		cemuLog_log(LogType::Force, "Invariant TSC not supported");
    #endif

	_mm_mfence();
	uint64 tscStart = __rdtsc();
	unsigned int startTime = GetTickCount();
	HRTick startTick = HighResolutionTimer::now().getTick();
	// wait roughly 3 seconds
	while (true)
	{
		if ((GetTickCount() - startTime) >= 3000)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	_mm_mfence();
	HRTick stopTick = HighResolutionTimer::now().getTick();
	uint64 tscEnd = __rdtsc();
	// derive frequency approximation from measured time difference
	uint64 tsc_diff = tscEnd - tscStart;
	uint64 hrtFreq = 0;
	uint64 hrtDiff = HighResolutionTimer::getTimeDiffEx(startTick, stopTick, hrtFreq);
	uint64 tsc_freq = muldiv64(tsc_diff, hrtFreq, hrtDiff);

	return tsc_freq;
}

int PPCTimer_initThread(uint64 hostCounterStart)
{
	uint64 frequency = PPCTimer_estimateRDTSCFrequency();
	std::unique_lock _l(s_timerEpochMutex);
	_rdtscFrequency = frequency;
	// PPCTimer_start() may have been called in the meantime, in which case its epoch takes precedence
	if (!s_timerEpoch.load(std::memory_order_relaxed))
		PPCTimer_publishEpoch(hostCounterStart, 0, ActiveSettings::GetTimerShiftFactor());
	return 0;
}

void PPCTimer_init()
{
	std::thread t(PPCTimer_initThread, PPCTimer_readHostCounter());
	t.detach();
}

#endif

void PPCTimer_start()
{
	std::unique_lock _l(s_timerEpochMutex);
	PPCTimer_publishEpoch(PPCTimer_readHostCounter(), 0, ActiveSettings::GetTimerShiftFactor());
	s_lastGuestTick.store(0, std::memory_order_relaxed);
}

uint64 PPCTimer_getRawTsc()
{
	return __rdtsc();
}

uint64 PPCTimer_microsecondsToTsc(uint64 us)
{
	return (us * _rdtscFrequency) / 1000000ULL;
}

uint64 PPCTimer_tscToMicroseconds(uint64 us)
{
	uint128_t r{};
	r.low = _umul128(us, 1000000ULL, &r.high);

	uint64 remainder;
	const uint64 microseconds = _udiv128(r.high, r.low, _rdtscFrequency, &remainder);

	return microseconds;
}

bool PPCTimer_isReady()
{
	return s_timerEpoch.load(std::memory_order_acquire) != nullptr;
}

void PPCTimer_waitForInit()
{
	while (!PPCTimer_isReady()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// called when the timer speed setting changed. Starts a new epoch at the current guest time so the timebase stays continuous
static const PPCTimerEpoch* PPCTimer_changeShiftFactor(uint8 shiftFactor)
{
	std::unique_lock _l(s_timerEpochMutex);
	const PPCTimerEpoch* epoch = s_timerEpoch.load(std::memory_order_acquire);
	if (epoch->shiftFactor == shiftFactor)
		return epoch; // another thread already switched
	const uint64 hostNow = PPCTimer_readHostCounter();
	const uint64 guestNow = std::max(PPCTimer_epochToGuestTick(epoch, hostNow), s_lastGuestTick.load(std::memory_order_relaxed));
	return PPCTimer_publishEpoch(hostNow, guestNow, shiftFactor);
}

// thread safe and lock-free
uint64 PPCTimer_getFromRDTSC()
{
	const PPCTimerEpoch* epoch = s_timerEpoch.load(std::memory_order_acquire);
	if (!epoch)
		return 0; // not initialized yet
	const uint8 shiftFactor = ActiveSettings::GetTimerShiftFactor();
	if (epoch->shiftFactor != shiftFactor)
		epoch = PPCTimer_changeShiftFactor(shiftFactor);
	const uint64 guestTick = PPCTimer_epochToGuestTick(epoch, PPCTimer_readHostCounter());
	// only travel forward in time, even if another thread already returned a later value
	uint64 lastGuestTick = s_lastGuestTick.load(std::memory_order_relaxed);
	while (guestTick > lastGuestTick)
	{
		if (s_lastGuestTick.compare_exchange_weak(lastGuestTick, guestTick, std::memory_order_relaxed))
			return guestTick;
	}
	return lastGuestTick;
}
