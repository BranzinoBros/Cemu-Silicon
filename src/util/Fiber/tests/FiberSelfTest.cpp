// Standalone self-test and benchmark for the Fiber implementation
// - ping-pongs between several fibers and checks that callee-saved integer and FP registers survive every switch
// - switches fiber to fiber, runs fibers on multiple host threads at once and resumes a fiber on a different host thread
// - checks stack alignment, per-fiber private data and (if available) that overflowing a fiber stack faults on the guard page
// - prints the switch rate
// Usage: FiberSelfTest [rounds]

#include "Fiber.h"
#include <chrono>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
	uint64 s_errorCount = 0;

	void ReportError(const char* testName, const char* message)
	{
		fprintf(stderr, "[%s] FAILED: %s\n", testName, message);
		s_errorCount++;
	}

	void SwitchToTarget(void* targetFiber)
	{
		Fiber::Switch(*(Fiber*)targetFiber);
	}

#if defined(__aarch64__) && defined(__clang__)
	// calls fn(arg) with x19-x28 and d8-d15 set to values derived from seed
	// returns the number of these registers which did not hold their value when fn returned
	__attribute__((naked, noinline)) uint64 CallWithRegisterPattern(void(*fn)(void*), void* arg, uint64 seed)
	{
		asm volatile(
		"stp x29, x30, [sp, #-176]!\n"
		"mov x29, sp\n"
		"stp x19, x20, [sp, #16]\n"
		"stp x21, x22, [sp, #32]\n"
		"stp x23, x24, [sp, #48]\n"
		"stp x25, x26, [sp, #64]\n"
		"stp x27, x28, [sp, #80]\n"
		"stp d8, d9, [sp, #96]\n"
		"stp d10, d11, [sp, #112]\n"
		"stp d12, d13, [sp, #128]\n"
		"stp d14, d15, [sp, #144]\n"
		"str x2, [sp, #160]\n"
		"add x19, x2, #19\n"
		"add x20, x2, #20\n"
		"add x21, x2, #21\n"
		"add x22, x2, #22\n"
		"add x23, x2, #23\n"
		"add x24, x2, #24\n"
		"add x25, x2, #25\n"
		"add x26, x2, #26\n"
		"add x27, x2, #27\n"
		"add x28, x2, #28\n"
		"add x9, x2, #108\n"
		"fmov d8, x9\n"
		"add x9, x2, #109\n"
		"fmov d9, x9\n"
		"add x9, x2, #110\n"
		"fmov d10, x9\n"
		"add x9, x2, #111\n"
		"fmov d11, x9\n"
		"add x9, x2, #112\n"
		"fmov d12, x9\n"
		"add x9, x2, #113\n"
		"fmov d13, x9\n"
		"add x9, x2, #114\n"
		"fmov d14, x9\n"
		"add x9, x2, #115\n"
		"fmov d15, x9\n"
		"mov x9, x0\n"
		"mov x0, x1\n"
		"blr x9\n"
		"ldr x2, [sp, #160]\n"
		"mov x0, xzr\n"
		"add x9, x2, #19\n"
		"cmp x19, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #20\n"
		"cmp x20, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #21\n"
		"cmp x21, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #22\n"
		"cmp x22, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #23\n"
		"cmp x23, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #24\n"
		"cmp x24, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #25\n"
		"cmp x25, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #26\n"
		"cmp x26, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #27\n"
		"cmp x27, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #28\n"
		"cmp x28, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #108\n"
		"fmov x10, d8\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #109\n"
		"fmov x10, d9\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #110\n"
		"fmov x10, d10\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #111\n"
		"fmov x10, d11\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #112\n"
		"fmov x10, d12\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #113\n"
		"fmov x10, d13\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #114\n"
		"fmov x10, d14\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"add x9, x2, #115\n"
		"fmov x10, d15\n"
		"cmp x10, x9\n"
		"cinc x0, x0, ne\n"
		"ldp x19, x20, [sp, #16]\n"
		"ldp x21, x22, [sp, #32]\n"
		"ldp x23, x24, [sp, #48]\n"
		"ldp x25, x26, [sp, #64]\n"
		"ldp x27, x28, [sp, #80]\n"
		"ldp d8, d9, [sp, #96]\n"
		"ldp d10, d11, [sp, #112]\n"
		"ldp d12, d13, [sp, #128]\n"
		"ldp d14, d15, [sp, #144]\n"
		"ldp x29, x30, [sp], #176\n"
		"ret\n"
		);
	}

	constexpr bool HAS_REGISTER_CHECK = true;

	uint64 SwitchWithRegisterCheck(Fiber* targetFiber, uint64 seed)
	{
		return CallWithRegisterPattern(SwitchToTarget, targetFiber, seed);
	}
#else
	constexpr bool HAS_REGISTER_CHECK = false;

	uint64 SwitchWithRegisterCheck(Fiber* targetFiber, uint64 seed)
	{
		SwitchToTarget(targetFiber);
		return 0;
	}
#endif

	struct Worker
	{
		uint64 id{};
		Fiber* fiber{};
		Fiber* volatile returnTo{};
		uint64 rounds{};
		uint64 registerErrors{};
		uint64 privateDataErrors{};
		bool misalignedStack{};
	};

	void WorkerEntry(void* userParam)
	{
		Worker* worker = (Worker*)userParam;
		if (((uintptr_t)__builtin_frame_address(0) & 15) != 0)
			worker->misalignedStack = true;
		while (true)
		{
			if (Fiber::GetFiberPrivateData() != worker)
				worker->privateDataErrors++;
			uint64 seed = (worker->id << 40) ^ (worker->rounds << 12) ^ 0x5A5A000000000000ull;
			worker->registerErrors += SwitchWithRegisterCheck(worker->returnTo, seed);
			worker->rounds++;
		}
	}

	struct WorkerSet
	{
		WorkerSet(size_t count, uint64 firstId)
		{
			workers.resize(count);
			for (size_t i = 0; i < count; i++)
			{
				workers[i].id = firstId + i;
				workers[i].fiber = new Fiber(WorkerEntry, &workers[i], &workers[i]);
			}
		}

		~WorkerSet()
		{
			for (auto& worker : workers)
				delete worker.fiber;
		}

		bool Check(const char* testName, uint64 expectedRoundsPerWorker)
		{
			bool ok = true;
			for (auto& worker : workers)
			{
				char msg[256];
				if (worker.registerErrors)
				{
					snprintf(msg, sizeof(msg), "worker %llu: %llu callee-saved register mismatches", (unsigned long long)worker.id, (unsigned long long)worker.registerErrors);
					ReportError(testName, msg);
					ok = false;
				}
				if (worker.privateDataErrors)
				{
					snprintf(msg, sizeof(msg), "worker %llu: GetFiberPrivateData() returned wrong pointer %llu times", (unsigned long long)worker.id, (unsigned long long)worker.privateDataErrors);
					ReportError(testName, msg);
					ok = false;
				}
				if (worker.misalignedStack)
				{
					snprintf(msg, sizeof(msg), "worker %llu: fiber stack frame not 16-byte aligned", (unsigned long long)worker.id);
					ReportError(testName, msg);
					ok = false;
				}
				if (worker.rounds != expectedRoundsPerWorker)
				{
					snprintf(msg, sizeof(msg), "worker %llu: ran %llu rounds, expected %llu", (unsigned long long)worker.id, (unsigned long long)worker.rounds, (unsigned long long)expectedRoundsPerWorker);
					ReportError(testName, msg);
					ok = false;
				}
			}
			return ok;
		}

		std::vector<Worker> workers;
	};

	// main fiber <-> worker ping-pong, round robin over all workers
	void RunPingPong(const char* testName, Fiber* mainFiber, size_t workerCount, uint64 rounds, uint64 firstId)
	{
		WorkerSet set(workerCount, firstId);
		uint64 mainErrors = 0;
		for (auto& worker : set.workers)
			worker.returnTo = mainFiber;
		for (uint64 round = 0; round < rounds; round++)
		{
			for (auto& worker : set.workers)
			{
				if (Fiber::GetFiberPrivateData() != nullptr)
					mainErrors++;
				mainErrors += SwitchWithRegisterCheck(worker.fiber, (round << 16) ^ (worker.id << 4) ^ 0xC3C3000000000000ull);
			}
		}
		// each worker has been entered `rounds` times but has only completed `rounds - 1` rounds since it is suspended in the last one
		bool ok = set.Check(testName, rounds - 1);
		if (mainErrors)
		{
			ReportError(testName, "main fiber lost callee-saved registers or private data");
			ok = false;
		}
		printf("[%s] %s (%zu fibers, %llu switches)\n", testName, ok ? "ok" : "FAILED", workerCount, (unsigned long long)(rounds * workerCount * 2));
	}

	// fibers switch directly to each other: main -> w0 -> w1 -> ... -> wN -> main
	void RunRing(Fiber* mainFiber, uint64 rounds)
	{
		const char* testName = "ring";
		WorkerSet set(4, 100);
		for (size_t i = 0; i < set.workers.size(); i++)
			set.workers[i].returnTo = (i + 1 < set.workers.size()) ? set.workers[i + 1].fiber : mainFiber;
		uint64 mainErrors = 0;
		for (uint64 round = 0; round < rounds; round++)
			mainErrors += SwitchWithRegisterCheck(set.workers[0].fiber, round ^ 0x3C3C000000000000ull);
		bool ok = set.Check(testName, rounds - 1);
		if (mainErrors)
		{
			ReportError(testName, "main fiber lost callee-saved registers");
			ok = false;
		}
		printf("[%s] %s (%zu fibers, %llu switches)\n", testName, ok ? "ok" : "FAILED", set.workers.size(), (unsigned long long)(rounds * (set.workers.size() + 1)));
	}

	// several host threads each running their own fibers at the same time (like the OSSched[core=N] threads)
	void RunMultiThreaded(uint64 rounds)
	{
		std::vector<std::thread> threads;
		for (uint64 t = 0; t < 3; t++)
		{
			threads.emplace_back([t, rounds]() {
				Fiber* threadFiber = Fiber::PrepareCurrentThread();
				RunPingPong("multi-thread", threadFiber, 3, rounds, 1000 + t * 10);
			});
		}
		for (auto& thread : threads)
			thread.join();
	}

	// a suspended fiber is resumed on a different host thread
	void RunMigration(Fiber* mainFiber)
	{
		const char* testName = "migration";
		WorkerSet set(1, 200);
		Worker& worker = set.workers[0];
		worker.returnTo = mainFiber;
		SwitchToTarget(worker.fiber);
		std::thread otherThread([&worker]() {
			Fiber* threadFiber = Fiber::PrepareCurrentThread();
			worker.returnTo = threadFiber;
			for (uint64 i = 0; i < 1000; i++)
				SwitchToTarget(worker.fiber);
		});
		otherThread.join();
		worker.returnTo = mainFiber;
		SwitchToTarget(worker.fiber);
		bool ok = set.Check(testName, 1001);
		printf("[%s] %s\n", testName, ok ? "ok" : "FAILED");
	}

	void BenchmarkEntry(void* userParam)
	{
		Fiber* returnTo = (Fiber*)userParam;
		while (true)
			Fiber::Switch(*returnTo);
	}

	void RunBenchmark(Fiber* mainFiber, uint64 rounds)
	{
		Fiber benchmarkFiber(BenchmarkEntry, mainFiber, nullptr);
		Fiber::Switch(benchmarkFiber); // warm up
		auto start = std::chrono::steady_clock::now();
		for (uint64 i = 0; i < rounds; i++)
			Fiber::Switch(benchmarkFiber);
		auto end = std::chrono::steady_clock::now();
		double seconds = std::chrono::duration<double>(end - start).count();
		double switches = (double)rounds * 2.0;
		printf("[benchmark] %.0f switches in %.3f s: %.1f million switches/s, %.1f ns per switch\n", switches, seconds, switches / seconds / 1000000.0, seconds * 1000000000.0 / switches);
	}

#if defined(FIBER_SELFTEST_HAS_GUARD_PAGE)
	__attribute__((noinline)) uint64 RecurseUntilFault(uint64 depth)
	{
		volatile uint8_t buffer[4096];
		for (size_t i = 0; i < sizeof(buffer); i += 256)
			buffer[i] = (uint8_t)depth;
		if (depth >= 100000)
			return depth;
		return RecurseUntilFault(depth + 1) + buffer[0];
	}

	void OverflowEntry(void* userParam)
	{
		RecurseUntilFault(0);
		_exit(0); // reaching this means the stack overflow went unnoticed
	}

	// overflowing a fiber stack must fault on the guard page instead of corrupting adjacent memory
	void RunGuardPage(Fiber* mainFiber)
	{
		const char* testName = "guard-page";
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0)
		{
			signal(SIGSEGV, SIG_DFL);
			signal(SIGBUS, SIG_DFL);
			Fiber overflowFiber(OverflowEntry, nullptr, nullptr);
			Fiber::Switch(overflowFiber);
			_exit(0);
		}
		int status = 0;
		if (pid < 0 || waitpid(pid, &status, 0) != pid)
		{
			ReportError(testName, "fork/waitpid failed");
			return;
		}
		bool ok = WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
		if (!ok)
			ReportError(testName, "stack overflow did not fault");
		printf("[%s] %s (child status 0x%x)\n", testName, ok ? "ok" : "FAILED", status);
	}
#endif
}

int main(int argc, char* argv[])
{
	uint64 rounds = 500000;
	if (argc > 1)
		rounds = strtoull(argv[1], nullptr, 10);
	if (rounds < 2)
		rounds = 2;
	printf("Fiber self-test, page size %ld, register check %s\n", sysconf(_SC_PAGESIZE), HAS_REGISTER_CHECK ? "enabled" : "unavailable");

	Fiber* mainFiber = Fiber::PrepareCurrentThread();
	RunPingPong("ping-pong", mainFiber, 4, rounds, 1);
	RunRing(mainFiber, rounds);
	RunMultiThreaded(rounds / 4 + 2);
	RunMigration(mainFiber);
#if defined(FIBER_SELFTEST_HAS_GUARD_PAGE)
	RunGuardPage(mainFiber);
#endif
	RunBenchmark(mainFiber, rounds * 4);

	if (s_errorCount)
	{
		printf("%llu error(s)\n", (unsigned long long)s_errorCount);
		return 1;
	}
	printf("All fiber tests passed\n");
	return 0;
}
