// ppc_bench: CPU bound microbenchmarks for Cemu's PowerPC emulation.
//
// Every benchmark performs a fixed amount of work and returns a checksum so the
// work can't be optimized away (and so recompiler and interpreter results can
// be compared). Each benchmark first runs two short warm-ups, each followed
// by a sleep, so Cemu's recompiler can translate the code before the timed
// run.
//
// Output format:
//   PPC_BENCH_BEGIN
//   bench <name>: us=<guest microseconds> checksum=<hex>
//   bench_total: us=<sum>
//   PPC_BENCH_DONE

#include "common.h"

#define WARMUP_SLEEP_MS 150

/* --- tight integer loop --- */

typedef struct
{
	u32 a, b, c;
} IntState;

static NOINLINE void IntKernel(IntState* s, u32 base, u32 n)
{
	u32 a = s->a, b = s->b, c = s->c;
	for (u32 i = base; i < base + n; i++)
	{
		a += b ^ i;
		b = (b << 5) | (b >> 27);
		c = c * 3 + (a >> 7);
		b -= c & 0xFF;
	}
	s->a = a;
	s->b = b;
	s->c = c;
}

#define CHUNK 4096

// the work is split into calls of CHUNK iterations. Cemu enters recompiled
// code at function entries, so this measures recompiled loop throughput
static NOINLINE u32 BenchIntLoop(u32 n)
{
	IntState s = {0x12345678, 0x9ABCDEF0, 1};
	for (u32 i = 0; i < n; i += CHUNK)
		IntKernel(&s, i, CHUNK);
	return s.a ^ s.b ^ s.c;
}

// same work as one long loop without calls; compare with int_loop to see the
// cost of the function-to-function transitions
static NOINLINE u32 BenchIntLongLoop(u32 n)
{
	IntState s = {0x12345678, 0x9ABCDEF0, 1};
	IntKernel(&s, 0, n);
	return s.a ^ s.b ^ s.c;
}

/* --- branchy code with data dependent branches --- */

static NOINLINE u32 BenchBranchy(u32 n)
{
	u32 seed = 1, acc = 0, steps = 0;
	for (u32 i = 0; i < n; i++)
	{
		seed = seed * 1664525u + 1013904223u;
		u32 v = seed >> 16;
		switch (v & 7)
		{
		case 0: acc += v; break;
		case 1: acc ^= v << 3; break;
		case 2: acc -= v >> 1; break;
		case 3: acc = (acc << 1) | (acc >> 31); break;
		case 4:
			if (v & 0x100)
				acc += 7;
			else
				acc -= 3;
			break;
		case 5: acc |= v & 0x0F0F; break;
		case 6: acc &= ~(v & 0xF0F0); break;
		default:
			// short collatz walk
			while (v > 1 && steps < 0xFFFFFFF0)
			{
				v = (v & 1) ? (v * 3 + 1) : (v >> 1);
				steps++;
				if (v & 0x8000)
					break;
			}
			break;
		}
	}
	return acc ^ steps;
}

/* --- double precision math --- */

static NOINLINE u32 BenchFpDouble(u32 n)
{
	double x = 0.5, y = 1.25, z = 0.0;
	for (u32 i = 0; i < n; i++)
	{
		// logistic map + Newton step for sqrt(2), keeps values bounded
		x = 3.7 * x * (1.0 - x);
		y = 0.5 * (y + 2.0 / y);
		z = z * 0.999 + x * y;
	}
	u64 bx, bz;
	memcpy(&bx, &x, 8);
	memcpy(&bz, &z, 8);
	return (u32)(bx >> 32) ^ (u32)bx ^ (u32)(bz >> 32) ^ (u32)bz;
}

/* --- paired single math --- */

static float s_psData[8] __attribute__((aligned(16))) = {1.0f, 0.5f, -0.25f, 2.0f, 0.999f, 0.998f, 0.001f, 0.002f};
static float s_psOut[4] __attribute__((aligned(16)));

static NOINLINE void PairedSingleKernel(u32 n)
{
	u32 cnt = n;
	__asm__ volatile(
		"mtctr %[n]\n\t"
		"psq_l 1,0(%[o]),0,7\n\t"   // v0
		"psq_l 2,8(%[o]),0,7\n\t"   // v1
		"psq_l 3,16(%[d]),0,7\n\t"  // decay
		"psq_l 4,24(%[d]),0,7\n\t"  // add
		"1:\n\t"
		"ps_madd 1,1,3,4\n\t"
		"ps_madd 2,2,3,4\n\t"
		"ps_add 5,1,2\n\t"
		"ps_mul 6,5,3\n\t"
		"ps_sum0 7,6,5,6\n\t"
		"ps_merge10 8,1,2\n\t"
		"ps_nmsub 1,8,4,1\n\t"
		"ps_madds0 2,2,7,4\n\t"
		"bdnz 1b\n\t"
		"psq_st 1,0(%[o]),0,7\n\t"
		"psq_st 2,8(%[o]),0,7"
		: [n] "+r"(cnt)
		: [d] "b"(s_psData), [o] "b"(s_psOut)
		: "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "ctr", "memory");
}

static NOINLINE u32 BenchPairedSingle(u32 n)
{
	u32 gqr7;
	__asm__ volatile("mfspr %0,903" : "=r"(gqr7));
	__asm__ volatile("mtspr 903,%0" : : "r"(0));
	// the kernel keeps its state in s_psOut between chunks
	memcpy(s_psOut, s_psData, sizeof(s_psOut));
	for (u32 i = 0; i < n; i += CHUNK)
		PairedSingleKernel(CHUNK);
	__asm__ volatile("mtspr 903,%0" : : "r"(gqr7));
	u32 h = FNV_INIT;
	for (u32 i = 0; i < 4; i++)
	{
		u32 v;
		memcpy(&v, s_psOut + i, 4);
		h = Fnv1a(h, v);
	}
	return h;
}

/* --- memcpy style loads and stores --- */

#define MEM_WORDS 8192
static u32 s_memA[MEM_WORDS] __attribute__((aligned(64)));
static u32 s_memB[MEM_WORDS] __attribute__((aligned(64)));

static NOINLINE void CopyWords(u32* dst, const u32* src, u32 count)
{
	// explicit lwz/stw copy, four words per iteration
	u32 iter = count / 4;
	__asm__ volatile(
		"mtctr %[n]\n\t"
		"addi %[s],%[s],-4\n\t"
		"addi %[d],%[d],-4\n\t"
		"1:\n\t"
		"lwz 9,4(%[s])\n\t"
		"lwz 10,8(%[s])\n\t"
		"lwz 11,12(%[s])\n\t"
		"lwzu 12,16(%[s])\n\t"
		"stw 9,4(%[d])\n\t"
		"stw 10,8(%[d])\n\t"
		"stw 11,12(%[d])\n\t"
		"stwu 12,16(%[d])\n\t"
		"bdnz 1b"
		: [s] "+b"(src), [d] "+b"(dst), [n] "+r"(iter)
		:
		: "r9", "r10", "r11", "r12", "ctr", "memory");
}

static NOINLINE u32 BenchMemcpy(u32 n)
{
	for (u32 i = 0; i < MEM_WORDS; i++)
		s_memA[i] = i * 0x9E3779B9;
	for (u32 i = 0; i < n; i++)
	{
		CopyWords(s_memB, s_memA, MEM_WORDS);
		// small mutation so every pass moves different data
		s_memB[i & (MEM_WORDS - 1)] += i;
		CopyWords(s_memA, s_memB, MEM_WORDS);
	}
	u32 h = FNV_INIT;
	for (u32 i = 0; i < MEM_WORDS; i += 61)
		h = Fnv1a(h, s_memA[i]);
	return h;
}

/* --- deep call chains and indirect calls --- */

static NOINLINE u32 Fib(u32 n)
{
	if (n < 2)
		return n;
	return Fib(n - 1) + Fib(n - 2);
}

typedef u32 (*ChainFn)(u32 v, u32 depth);
static NOINLINE u32 ChainA(u32 v, u32 depth);
static NOINLINE u32 ChainB(u32 v, u32 depth);
static NOINLINE u32 ChainC(u32 v, u32 depth);
static NOINLINE u32 ChainD(u32 v, u32 depth);

// volatile so the compiler can't turn the indirect calls into direct calls
static ChainFn volatile s_chainTable[4] = {ChainA, ChainB, ChainC, ChainD};

static NOINLINE u32 ChainA(u32 v, u32 depth)
{
	if (depth == 0)
		return v;
	return s_chainTable[(v + 1) & 3](v * 3 + 1, depth - 1) ^ depth;
}

static NOINLINE u32 ChainB(u32 v, u32 depth)
{
	if (depth == 0)
		return v ^ 0x55;
	return s_chainTable[(v >> 3) & 3](v + depth, depth - 1) + 1;
}

static NOINLINE u32 ChainC(u32 v, u32 depth)
{
	if (depth == 0)
		return ~v;
	return s_chainTable[(v ^ depth) & 3]((v << 1) | (v >> 31), depth - 1) - v;
}

static NOINLINE u32 ChainD(u32 v, u32 depth)
{
	if (depth == 0)
		return v + 7;
	u32 r = ChainA(v ^ 0xA5A5, depth - 1); // direct call
	return r + (v & 0xF);
}

static NOINLINE u32 BenchCalls(u32 n)
{
	u32 acc = Fib(16 + (n > 1000 ? 8 : 0));
	for (u32 i = 0; i < n; i++)
		acc += s_chainTable[i & 3](i, 24);
	return acc;
}

/* --- lwarx/stwcx. loop --- */

static u32 s_atomicCounter __attribute__((aligned(64)));

static NOINLINE u32 BenchAtomic(u32 n)
{
	s_atomicCounter = 0;
	u32* p = &s_atomicCounter;
	for (u32 i = 0; i < n; i++)
	{
		u32 v;
		__asm__ volatile(
			"1: lwarx %[v],0,%[p]\n\t"
			"add %[v],%[v],%[i]\n\t"
			"stwcx. %[v],0,%[p]\n\t"
			"bne- 1b"
			: [v] "=&r"(v)
			: [p] "r"(p), [i] "r"(i | 1)
			: "cr0", "memory");
	}
	return s_atomicCounter;
}

typedef struct
{
	const char* name;
	u32 (*fn)(u32 n);
	u32 warmup;
	u32 work;
} Bench;

// work amounts are sized for roughly 0.1-0.5 s per benchmark with the recompiler
static const Bench kBenches[] = {
	{"int_loop", BenchIntLoop, CHUNK, 40000000},
	{"int_long_loop", BenchIntLongLoop, 1000, 40000000},
	{"branchy", BenchBranchy, 1000, 15000000},
	{"fp_double", BenchFpDouble, 1000, 20000000},
	{"paired_single", BenchPairedSingle, CHUNK, 20000000},
	{"memcpy", BenchMemcpy, 1, 1500},
	{"calls", BenchCalls, 10, 600000},
	{"atomic", BenchAtomic, 1000, 15000000},
};

int main(int argc, char** argv)
{
	OutLine l;
	u64 totalUs = 0;
	u32 checksum = FNV_INIT;
	PrintLine("PPC_BENCH_BEGIN");
	for (u32 i = 0; i < sizeof(kBenches) / sizeof(kBenches[0]); i++)
	{
		const Bench* b = kBenches + i;
		// two warm-up rounds: right after boot the recompiler may still be busy
		b->fn(b->warmup);
		SleepMs(WARMUP_SLEEP_MS);
		b->fn(b->warmup);
		SleepMs(WARMUP_SLEEP_MS);
		OSTime start = OSGetSystemTime();
		u32 result = b->fn(b->work);
		OSTime end = OSGetSystemTime();
		u64 us = OSTicksToMicroseconds(end - start);
		totalUs += us;
		checksum = Fnv1a(checksum, result);
		OutReset(&l);
		OutStr(&l, "bench ");
		OutStr(&l, b->name);
		OutStr(&l, ": us=");
		OutDec(&l, us);
		OutStr(&l, " checksum=");
		OutHex32(&l, result);
		OutFlush(&l);
	}
	OutReset(&l);
	OutStr(&l, "bench_total: us=");
	OutDec(&l, totalUs);
	OutStr(&l, " checksum=");
	OutHex32(&l, checksum);
	OutFlush(&l);
	PrintLine("PPC_BENCH_DONE");
	return 0;
}
