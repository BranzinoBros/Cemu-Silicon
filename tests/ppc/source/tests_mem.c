// Load/store tests: displacement, update and indexed forms, byte-reversed
// accesses, load/store multiple and string, dcbz, lwarx/stwcx. and FP
// load/store conversions.

#include "tests.h"

static u8 s_mem[256] __attribute__((aligned(64)));

static void FillMem(void)
{
	for (u32 i = 0; i < sizeof(s_mem); i++)
		s_mem[i] = (u8)(i * 0x1D + 0x83);
}

static void PutMemWords(u32 offset, u32 count)
{
	for (u32 i = 0; i < count; i++)
	{
		u32 v;
		memcpy(&v, s_mem + offset + i * 4, 4);
		Put(v);
	}
}

/* --- integer loads with displacement --- */

static NOINLINE void LoadsDisp(u8* p)
{
	u32 r0, r1, r2, r3, r4, r5, r6, r7;
	__asm__ volatile(
		"lbz %[r0],0(%[p])\n\t"
		"lbz %[r1],7(%[p])\n\t"
		"lhz %[r2],2(%[p])\n\t"
		"lhz %[r3],-2(%[p])\n\t"
		"lha %[r4],4(%[p])\n\t"
		"lha %[r5],10(%[p])\n\t"
		"lwz %[r6],8(%[p])\n\t"
		"lwz %[r7],-4(%[p])"
		: [r0] "=&r"(r0), [r1] "=&r"(r1), [r2] "=&r"(r2), [r3] "=&r"(r3),
		  [r4] "=&r"(r4), [r5] "=&r"(r5), [r6] "=&r"(r6), [r7] "=&r"(r7)
		: [p] "b"(p)
		: "memory");
	Put(r0); Put(r1); Put(r2); Put(r3); Put(r4); Put(r5); Put(r6); Put(r7);
}

static void RunLoadsDisp(const TestDef* t)
{
	(void)t;
	FillMem();
	for (u32 off = 16; off < 48; off += 3)
		LoadsDisp(s_mem + off);
}

/* --- integer loads with update --- */

#define GEN_LOADU(id, insn, disp) \
	static NOINLINE void loadu_##id(u8* base) \
	{ \
		u8* p = base; \
		u32 r; \
		__asm__ volatile( \
			insn " %[r]," #disp "(%[p])" \
			: [r] "=&r"(r), [p] "+b"(p) \
			: \
			: "memory"); \
		Put(r); \
		Put((u32)(p - base)); \
	}

#define LOADU_OPS(X) \
	X(lbzu, "lbzu", 5) X(lhzu, "lhzu", -6) X(lhau, "lhau", 12) X(lhau2, "lhau", 1) \
	X(lwzu, "lwzu", 8) X(lwzu2, "lwzu", -16)

LOADU_OPS(GEN_LOADU)

static void RunLoadsUpdate(const TestDef* t)
{
	(void)t;
	FillMem();
#define CALL_LOADU(id, insn, disp) loadu_##id(s_mem + 64);
	LOADU_OPS(CALL_LOADU)
#undef CALL_LOADU
}

/* --- indexed loads (with and without update) --- */

#define GEN_LOADX(id, insn) \
	static NOINLINE void loadx_##id(u8* base, u32 idx) \
	{ \
		u32 r; \
		__asm__ volatile( \
			insn " %[r],%[p],%[i]" \
			: [r] "=&r"(r) \
			: [p] "b"(base), [i] "r"(idx) \
			: "memory"); \
		Put(r); \
	}

#define GEN_LOADUX(id, insn) \
	static NOINLINE void loadux_##id(u8* base, u32 idx) \
	{ \
		u8* p = base; \
		u32 r; \
		__asm__ volatile( \
			insn " %[r],%[p],%[i]" \
			: [r] "=&r"(r), [p] "+b"(p) \
			: [i] "r"(idx) \
			: "memory"); \
		Put(r); \
		Put((u32)(p - base)); \
	}

#define LOADX_OPS(X) X(lbzx, "lbzx") X(lhzx, "lhzx") X(lhax, "lhax") X(lwzx, "lwzx") X(lhbrx, "lhbrx") X(lwbrx, "lwbrx")
#define LOADUX_OPS(X) X(lbzux, "lbzux") X(lhzux, "lhzux") X(lhaux, "lhaux") X(lwzux, "lwzux")

LOADX_OPS(GEN_LOADX)
LOADUX_OPS(GEN_LOADUX)

static void RunLoadsIndexed(const TestDef* t)
{
	static const u32 idx[] = {0, 2, 4, 6, 12, 0xFFFFFFF8};
	(void)t;
	FillMem();
	for (u32 i = 0; i < sizeof(idx) / sizeof(idx[0]); i++)
	{
#define CALL_LOADX(id, insn) loadx_##id(s_mem + 96, idx[i]);
#define CALL_LOADUX(id, insn) loadux_##id(s_mem + 96, idx[i]);
		LOADX_OPS(CALL_LOADX)
		LOADUX_OPS(CALL_LOADUX)
#undef CALL_LOADX
#undef CALL_LOADUX
	}
}

/* --- stores --- */

static NOINLINE void StoresDisp(u8* p, u32 v)
{
	__asm__ volatile(
		"stb %[v],0(%[p])\n\t"
		"stb %[v],5(%[p])\n\t"
		"sth %[v],2(%[p])\n\t"
		"sth %[v],9(%[p])\n\t"
		"stw %[v],12(%[p])\n\t"
		"stw %[v],-4(%[p])"
		:
		: [p] "b"(p), [v] "r"(v)
		: "memory");
}

#define GEN_STOREU(id, insn, disp) \
	static NOINLINE u32 storeu_##id(u8* base, u32 v) \
	{ \
		u8* p = base; \
		__asm__ volatile( \
			insn " %[v]," #disp "(%[p])" \
			: [p] "+b"(p) \
			: [v] "r"(v) \
			: "memory"); \
		return (u32)(p - base); \
	}

#define STOREU_OPS(X) X(stbu, "stbu", 3) X(sthu, "sthu", -2) X(stwu, "stwu", 8) X(stwu2, "stwu", -12)

STOREU_OPS(GEN_STOREU)

#define GEN_STOREX(id, insn) \
	static NOINLINE void storex_##id(u8* base, u32 idx, u32 v) \
	{ \
		__asm__ volatile( \
			insn " %[v],%[p],%[i]" \
			: \
			: [p] "b"(base), [i] "r"(idx), [v] "r"(v) \
			: "memory"); \
	}

#define GEN_STOREUX(id, insn) \
	static NOINLINE u32 storeux_##id(u8* base, u32 idx, u32 v) \
	{ \
		u8* p = base; \
		__asm__ volatile( \
			insn " %[v],%[p],%[i]" \
			: [p] "+b"(p) \
			: [i] "r"(idx), [v] "r"(v) \
			: "memory"); \
		return (u32)(p - base); \
	}

#define STOREX_OPS(X) X(stbx, "stbx") X(sthx, "sthx") X(stwx, "stwx") X(sthbrx, "sthbrx") X(stwbrx, "stwbrx")
#define STOREUX_OPS(X) X(stbux, "stbux") X(sthux, "sthux") X(stwux, "stwux")

STOREX_OPS(GEN_STOREX)
STOREUX_OPS(GEN_STOREUX)

static void RunStores(const TestDef* t)
{
	(void)t;
	memset(s_mem, 0, sizeof(s_mem));
	StoresDisp(s_mem + 8, 0x89ABCDEF);
	StoresDisp(s_mem + 30, 0x01020304);
	PutMemWords(0, 16);

	memset(s_mem, 0, sizeof(s_mem));
#define CALL_STOREU(id, insn, disp) Put(storeu_##id(s_mem + 32, 0xA1B2C3D4 + disp));
	STOREU_OPS(CALL_STOREU)
#undef CALL_STOREU
	PutMemWords(16, 8);

	memset(s_mem, 0, sizeof(s_mem));
	u32 off = 0;
#define CALL_STOREX(id, insn) storex_##id(s_mem + 64, off, 0x11223344 + off); off += 6;
	STOREX_OPS(CALL_STOREX)
#undef CALL_STOREX
#define CALL_STOREUX(id, insn) Put(storeux_##id(s_mem + 64, off, 0x55667788 + off)); off += 6;
	STOREUX_OPS(CALL_STOREUX)
#undef CALL_STOREUX
	PutMemWords(64, 16);
}

/* --- load/store multiple --- */

static NOINLINE void Lmw(u8* p)
{
	register u32 o27 __asm__("r27");
	register u32 o28 __asm__("r28");
	register u32 o29 __asm__("r29");
	register u32 o30 __asm__("r30");
	register u32 o31 __asm__("r31");
	__asm__ volatile(
		"lmw 27,4(%[p])"
		: "=&r"(o27), "=&r"(o28), "=&r"(o29), "=&r"(o30), "=&r"(o31)
		: [p] "b"(p)
		: "memory");
	Put(o27); Put(o28); Put(o29); Put(o30); Put(o31);
}

static NOINLINE void Stmw(u8* p, u32 seed)
{
	register u32 i28 __asm__("r28") = seed;
	register u32 i29 __asm__("r29") = seed * 3;
	register u32 i30 __asm__("r30") = ~seed;
	register u32 i31 __asm__("r31") = seed ^ 0x5555AAAA;
	__asm__ volatile(
		"stmw 28,-8(%[p])"
		:
		: [p] "b"(p), "r"(i28), "r"(i29), "r"(i30), "r"(i31)
		: "memory");
}

static void RunLoadStoreMultiple(const TestDef* t)
{
	(void)t;
	FillMem();
	Lmw(s_mem + 16);
	Lmw(s_mem + 100);
	memset(s_mem, 0, sizeof(s_mem));
	Stmw(s_mem + 40, 0x13579BDF);
	PutMemWords(24, 10);
}

/* --- load/store string --- */

static NOINLINE void Lswi(u8* p)
{
	u32 a, b, c, d;
	__asm__ volatile(
		"li 5,-1\n\t"
		"li 6,-1\n\t"
		"li 7,-1\n\t"
		"li 8,-1\n\t"
		"lswi 5,%[p],7\n\t"
		"mr %[a],5\n\t"
		"mr %[b],6\n\t"
		"lswi 5,%[p],13\n\t"
		"mr %[c],7\n\t"
		"mr %[d],8\n\t"
		: [a] "=&r"(a), [b] "=&r"(b), [c] "=&r"(c), [d] "=&r"(d)
		: [p] "b"(p)
		: "r5", "r6", "r7", "r8", "memory");
	Put(a); Put(b); Put(c); Put(d);
}

static NOINLINE void Stswi(u8* p, u32 v)
{
	__asm__ volatile(
		"mr 5,%[v]\n\t"
		"not 6,%[v]\n\t"
		"rotlwi 7,%[v],8\n\t"
		"stswi 5,%[p],11\n\t"
		"stswi 6,%[p],1"
		:
		: [p] "b"(p), [v] "r"(v)
		: "r5", "r6", "r7", "memory");
}

static NOINLINE void Lswx(u8* p, u32 idx, u32 count)
{
	u32 a, b;
	__asm__ volatile(
		"li 5,-1\n\t"
		"li 6,-1\n\t"
		"mtxer %[n]\n\t"
		"lswx 5,%[p],%[i]\n\t"
		"mr %[a],5\n\t"
		"mr %[b],6"
		: [a] "=&r"(a), [b] "=&r"(b)
		: [p] "b"(p), [i] "r"(idx), [n] "r"(count)
		: "r5", "r6", "xer", "memory");
	Put(a); Put(b);
}

static NOINLINE void Stswx(u8* p, u32 idx, u32 count, u32 v)
{
	__asm__ volatile(
		"mr 5,%[v]\n\t"
		"not 6,%[v]\n\t"
		"mtxer %[n]\n\t"
		"stswx 5,%[p],%[i]"
		:
		: [p] "b"(p), [i] "r"(idx), [n] "r"(count), [v] "r"(v)
		: "r5", "r6", "xer", "memory");
}

static void RunString(const TestDef* t)
{
	(void)t;
	FillMem();
	Lswi(s_mem + 5);
	Lswx(s_mem + 32, 3, 6);
	Lswx(s_mem + 32, 1, 1);
	memset(s_mem, 0, sizeof(s_mem));
	Stswi(s_mem + 1, 0xDEADBEEF);
	Stswx(s_mem + 20, 2, 7, 0xCAFEF00D);
	Stswx(s_mem + 40, 0, 3, 0x76543210);
	PutMemWords(0, 12);
}

/* --- dcbz --- */

static NOINLINE void Dcbz(u8* p, u32 idx)
{
	__asm__ volatile("dcbz %[p],%[i]" : : [p] "b"(p), [i] "r"(idx) : "memory");
}

static void RunDcbz(const TestDef* t)
{
	(void)t;
	memset(s_mem, 0xFF, sizeof(s_mem));
	Dcbz(s_mem, 40);        // clears 32..63
	Dcbz(s_mem + 128, 31);  // clears 128..159
	for (u32 i = 0; i < 192; i += 16)
		PutMemWords(i, 1);
	PutMemWords(28, 10);
	PutMemWords(124, 10);
}

/* --- lwarx / stwcx. --- */

static u32 s_atomicWord __attribute__((aligned(32)));

static NOINLINE u32 LwarxStwcx(u32* p, u32 add, u32 xin)
{
	u32 old, nv, c;
	__asm__ volatile(
		"mtxer %[xi]\n\t"
		"mtcrf 0x80,%[z]\n\t"
		"lwarx %[old],0,%[p]\n\t"
		"add %[nv],%[old],%[add]\n\t"
		"stwcx. %[nv],0,%[p]\n\t"
		"mfcr %[c]"
		: [old] "=&r"(old), [nv] "=&r"(nv), [c] "=&r"(c)
		: [p] "r"(p), [add] "r"(add), [xi] "r"(xin), [z] "r"(0)
		: "cr0", "xer", "memory");
	Put(old);
	return c >> 28;
}

// stwcx. right after a successful stwcx. has no reservation and must fail
static NOINLINE u32 StwcxNoReservation(u32* p, u32 v)
{
	u32 c;
	__asm__ volatile(
		"mtcrf 0x80,%[z]\n\t"
		"stwcx. %[v],0,%[p]\n\t"
		"mfcr %[c]"
		: [c] "=&r"(c)
		: [p] "r"(p), [v] "r"(v), [z] "r"(0)
		: "cr0", "memory");
	return c >> 28;
}

static NOINLINE u32 AtomicIncLoop(u32* p, u32 n)
{
	u32 tries = 0;
	for (u32 i = 0; i < n; i++)
	{
		u32 v;
		__asm__ volatile(
			"1: lwarx %[v],0,%[p]\n\t"
			"addi %[v],%[v],3\n\t"
			"stwcx. %[v],0,%[p]\n\t"
			"addi %[t],%[t],1\n\t"
			"bne- 1b"
			: [v] "=&r"(v), [t] "+&r"(tries)
			: [p] "r"(p)
			: "cr0", "memory");
	}
	return tries;
}

static void RunAtomic(const TestDef* t)
{
	(void)t;
	s_atomicWord = 0x7FFFFFFE;
	Put(LwarxStwcx(&s_atomicWord, 5, 0));
	Put(s_atomicWord);
	Put(StwcxNoReservation(&s_atomicWord, 0x12345678));
	Put(s_atomicWord);
	Put(LwarxStwcx(&s_atomicWord, 0xFFFFFFFF, 0));
	Put(s_atomicWord);
	// XER[SO] is copied into CR0 by stwcx.
	Put(LwarxStwcx(&s_atomicWord, 1, 0x80000000));
	Put(s_atomicWord);
	s_atomicWord = 0;
	Put(AtomicIncLoop(&s_atomicWord, 100));
	Put(s_atomicWord);
}

/* --- FP loads and stores --- */

static NOINLINE double Lfs(const u32* p)
{
	double d;
	__asm__ volatile("lfs %[d],0(%[p])" : [d] "=f"(d) : [p] "b"(p) : "memory");
	return d;
}

static NOINLINE u32 Stfs(double d)
{
	u32 v;
	__asm__ volatile("stfs %[d],%[m]" : [m] "=m"(v) : [d] "f"(d));
	return v;
}

static NOINLINE u64 LfdStfd(const u64* p)
{
	u64 v;
	__asm__ volatile(
		"lfd 0,0(%[p])\n\t"
		"stfd 0,%[m]"
		: [m] "=m"(v)
		: [p] "b"(p)
		: "fr0", "memory");
	return v;
}

static const u32 kFloatBits[] = {
	0x3F800000, // 1.0
	0x3F800001,
	0x00000001, // smallest denormal
	0x807FFFFF, // largest negative denormal
	0x00800000, // smallest normal
	0x7F7FFFFF, // max float
	0x7F800000, // inf
	0xFF800000, // -inf
	0x7FC00001, // qNaN with payload
	0x7F800001, // sNaN
	0xFFBFFFFF, // negative sNaN, full payload
	0x80000000, // -0
};
#define NUM_FLOAT_BITS (sizeof(kFloatBits) / sizeof(kFloatBits[0]))

static const u64 kStfsDoubles[] = {
	0x3FF0000000000000ull, // 1.0
	0x3FF0000010000000ull, // 1 + 2^-24, tie when rounding to single
	0x3FF0000030000000ull, // rounds up when rounding
	0x3FF00000FFFFFFFFull, // low bits that stfs must drop
	0x36A0000000000000ull, // 2^-149, single denormal min
	0x3690000000000000ull, // 2^-150, below single range
	0x3800000000000000ull, // 2^-127, single denormal
	0x47EFFFFFE0000000ull, // max float
	0x47F0000000000000ull, // 2^128, out of single range
	0x7FF0000000000000ull, // inf
	0x7FF8000000000001ull, // qNaN, payload only in low bits
	0x7FF4000020000000ull, // sNaN with payload in single range
	0xFFF0000000000001ull, // negative sNaN
	0x8000000000000001ull, // negative double denormal
};
#define NUM_STFS_DOUBLES (sizeof(kStfsDoubles) / sizeof(kStfsDoubles[0]))

static void RunFpLoadStore(const TestDef* t)
{
	(void)t;
	for (u32 i = 0; i < NUM_FLOAT_BITS; i++)
		PutD(Lfs(kFloatBits + i));
	for (u32 i = 0; i < NUM_STFS_DOUBLES; i++)
		Put(Stfs(BitsDouble(kStfsDoubles[i])));
	for (u32 i = 0; i < NUM_STFS_DOUBLES; i++)
		Put64(LfdStfd(kStfsDoubles + i));
}

// lfs -> stfs roundtrip must preserve all single precision bit patterns
static NOINLINE u32 LfsStfs(const u32* p)
{
	u32 v;
	__asm__ volatile(
		"lfs 0,0(%[p])\n\t"
		"stfs 0,%[m]"
		: [m] "=m"(v)
		: [p] "b"(p)
		: "fr0", "memory");
	return v;
}

static void RunFpRoundtrip(const TestDef* t)
{
	(void)t;
	for (u32 i = 0; i < NUM_FLOAT_BITS; i++)
		Put(LfsStfs(kFloatBits + i));
}

static NOINLINE void FpAddressing(u8* base)
{
	u8* p = base;
	double d0, d1, d2, d3;
	u32 off1, off2, off3;
	__asm__ volatile(
		"lfsu %[d0],8(%[p])\n\t"
		"mr %[o1],%[p]\n\t"
		"lfdu %[d1],16(%[p])\n\t"
		"mr %[o2],%[p]\n\t"
		"lfsx %[d2],%[p],%[i]\n\t"
		"lfdux %[d3],%[p],%[i]\n\t"
		"mr %[o3],%[p]\n\t"
		"stfsu %[d1],4(%[p])\n\t"
		"stfdx %[d0],%[p],%[i]\n\t"
		"stfdu %[d2],-24(%[p])\n\t"
		"stfsux %[d3],%[p],%[i]\n\t"
		"stfsx %[d0],%[p],%[i]\n\t"
		: [d0] "=&f"(d0), [d1] "=&f"(d1), [d2] "=&f"(d2), [d3] "=&f"(d3),
		  [o1] "=&r"(off1), [o2] "=&r"(off2), [o3] "=&r"(off3), [p] "+b"(p)
		: [i] "r"(8)
		: "memory");
	PutD(d0); PutD(d1); PutD(d2); PutD(d3);
	Put(off1 - (u32)base);
	Put(off2 - (u32)base);
	Put(off3 - (u32)base);
	Put((u32)(p - base));
}

static void RunFpAddressing(const TestDef* t)
{
	(void)t;
	FillMem();
	FpAddressing(s_mem + 32);
	PutMemWords(32, 24);
}

// fctiwz + stfiwx is the usual float->int conversion sequence
static NOINLINE u32 Stfiwx(double d)
{
	u32 v;
	__asm__ volatile(
		"fctiwz 0,%[d]\n\t"
		"stfiwx 0,0,%[p]"
		:
		: [d] "f"(d), [p] "r"(&v)
		: "fr0", "memory");
	return v;
}

static void RunStfiwx(const TestDef* t)
{
	static const double vals[] = {0.0, 1.9, -1.9, 1e20, -1e20, 65536.5, -0.0};
	(void)t;
	for (u32 i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
		Put(Stfiwx(vals[i]));
}

const TestDef g_memTests[] = {
	{"load_disp", RunLoadsDisp, 0},
	{"load_update", RunLoadsUpdate, 0},
	{"load_indexed", RunLoadsIndexed, 0},
	{"store", RunStores, 0},
	{"lmw_stmw", RunLoadStoreMultiple, 0},
	{"lswi_stswi", RunString, 0},
	{"dcbz", RunDcbz, 0},
	{"lwarx_stwcx", RunAtomic, 0},
	{"fp_load_store", RunFpLoadStore, 0},
	{"lfs_stfs", RunFpRoundtrip, 0},
	{"fp_addressing", RunFpAddressing, 0},
	{"stfiwx", RunStfiwx, 0},
};

const u32 g_memTestCount = sizeof(g_memTests) / sizeof(g_memTests[0]);
