// Paired single tests: arithmetic, merges, estimates, compares, quantized
// loads/stores (psq_l/psq_st with several GQR types and scales) and the
// interaction between scalar FP instructions and ps1.
//
// Results are read back as ps0 (stfd) and ps1 (ps_merge10 + stfd), both as
// full double bit patterns so precision bugs in the internal representation
// are visible.

#include "tests.h"

#define SPR_UGQR6 902
#define SPR_UGQR7 903

static NOINLINE u32 GetGqr6(void)
{
	u32 v;
	__asm__ volatile("mfspr %0,902" : "=r"(v));
	return v;
}

static NOINLINE void SetGqr6(u32 v)
{
	__asm__ volatile("mtspr 902,%0" : : "r"(v));
}

static NOINLINE u32 GetGqr7(void)
{
	u32 v;
	__asm__ volatile("mfspr %0,903" : "=r"(v));
	return v;
}

static NOINLINE void SetGqr7(u32 v)
{
	__asm__ volatile("mtspr 903,%0" : : "r"(v));
}

// GQR layout: load scale [24:29], load type [16:18], store scale [8:13], store type [0:2]
#define GQR(ldType, ldScale, stType, stScale) \
	((((u32)(ldScale) & 0x3F) << 24) | (((u32)(ldType) & 7) << 16) | (((u32)(stScale) & 0x3F) << 8) | ((u32)(stType) & 7))

#define QT_FLOAT 0
#define QT_U8 4
#define QT_U16 5
#define QT_S8 6
#define QT_S16 7

// GQR7 is used for plain float loads/stores in this file, GQR6 for quantization tests
static u32 s_savedGqr6, s_savedGqr7;

static void BeginPs(void)
{
	s_savedGqr6 = GetGqr6();
	s_savedGqr7 = GetGqr7();
	SetGqr7(0);
}

static void EndPs(void)
{
	SetGqr6(s_savedGqr6);
	SetGqr7(s_savedGqr7);
}

static const u32 kPairs[][2] __attribute__((aligned(8))) = {
	{0x3F800000, 0xC0000000}, // 1.0, -2.0
	{0x3F000000, 0x7F61B1E6}, // 0.5, 3e38
	{0x80000000, 0x000116C2}, // -0.0, 1e-40 (denormal)
	{0x7F800000, 0xFF800000}, // inf, -inf
	{0x7FC00123, 0x40200000}, // qNaN payload, 2.5
	{0x3FC00000, 0x3DCCCCCD}, // 1.5, 0.1
	{0x7F7FFFFF, 0xFF7FFFFF}, // max, -max
	{0x7F800042, 0x00800000}, // sNaN, smallest normal
};
#define NUM_PAIRS (sizeof(kPairs) / sizeof(kPairs[0]))

static u64 s_out[4] __attribute__((aligned(8)));

static void PutOut(u32 count)
{
	for (u32 i = 0; i < count; i++)
		Put64(s_out[i]);
}

/* --- binary ops: insn frD, frA, frB (or frA, frC for the multiplies) --- */

typedef void (*Ps2Fn)(const u32* a, const u32* b);

#define GEN_PS2(id, mn) \
	static NOINLINE void ps2_##id(const u32* a, const u32* b) \
	{ \
		__asm__ volatile( \
			"psq_l 1,0(%[a]),0,7\n\t" \
			"psq_l 2,0(%[b]),0,7\n\t" \
			mn " 3,1,2\n\t" \
			"stfd 3,0(%[o])\n\t" \
			"ps_merge10 4,3,3\n\t" \
			"stfd 4,8(%[o])" \
			: \
			: [a] "b"(a), [b] "b"(b), [o] "b"(s_out) \
			: "fr1", "fr2", "fr3", "fr4", "memory"); \
	}

#define PS_OPS2(X) \
	X(ps_add, "ps_add") X(ps_sub, "ps_sub") X(ps_mul, "ps_mul") X(ps_div, "ps_div") \
	X(ps_muls0, "ps_muls0") X(ps_muls1, "ps_muls1") \
	X(ps_merge00, "ps_merge00") X(ps_merge01, "ps_merge01") \
	X(ps_merge10, "ps_merge10") X(ps_merge11, "ps_merge11")

PS_OPS2(GEN_PS2)

static void RunPs2(const TestDef* t)
{
	Ps2Fn fn = (Ps2Fn)t->ctx;
	BeginPs();
	for (u32 i = 0; i < NUM_PAIRS; i++)
	{
		for (u32 j = 0; j < NUM_PAIRS; j++)
		{
			fn(kPairs[i], kPairs[j]);
			PutOut(2);
		}
	}
	EndPs();
}

/* --- ternary ops: insn frD, frA, frC, frB --- */

typedef void (*Ps3Fn)(const u32* a, const u32* c, const u32* b);

#define GEN_PS3(id, mn) \
	static NOINLINE void ps3_##id(const u32* a, const u32* c, const u32* b) \
	{ \
		__asm__ volatile( \
			"psq_l 1,0(%[a]),0,7\n\t" \
			"psq_l 2,0(%[c]),0,7\n\t" \
			"psq_l 3,0(%[b]),0,7\n\t" \
			mn " 4,1,2,3\n\t" \
			"stfd 4,0(%[o])\n\t" \
			"ps_merge10 5,4,4\n\t" \
			"stfd 5,8(%[o])" \
			: \
			: [a] "b"(a), [b] "b"(b), [c] "b"(c), [o] "b"(s_out) \
			: "fr1", "fr2", "fr3", "fr4", "fr5", "memory"); \
	}

#define PS_OPS3(X) \
	X(ps_madd, "ps_madd") X(ps_msub, "ps_msub") X(ps_nmadd, "ps_nmadd") X(ps_nmsub, "ps_nmsub") \
	X(ps_madds0, "ps_madds0") X(ps_madds1, "ps_madds1") \
	X(ps_sum0, "ps_sum0") X(ps_sum1, "ps_sum1") X(ps_sel, "ps_sel")

PS_OPS3(GEN_PS3)

static void RunPs3(const TestDef* t)
{
	Ps3Fn fn = (Ps3Fn)t->ctx;
	BeginPs();
	for (u32 i = 0; i < NUM_PAIRS; i++)
	{
		for (u32 j = 0; j < NUM_PAIRS; j++)
		{
			fn(kPairs[i], kPairs[j], kPairs[(i + j + 1) % NUM_PAIRS]);
			PutOut(2);
		}
	}
	EndPs();
}

// values where fused and unfused multiply-add differ
static const u32 kFmaPairs[][3][2] __attribute__((aligned(8))) = {
	// a, c, b
	{{0x3F800800, 0x3F800001}, {0x3F800800, 0x3F7FFFFE}, {0xBF800000, 0xBF800000}},
	{{0x40490FDB, 0x3DCCCCCD}, {0x40490FDB, 0x41200000}, {0xC11DE9E7, 0xBF800000}},
	{{0x7F7FFFFF, 0x00800000}, {0x40000000, 0x3F000000}, {0xFF7FFFFF, 0x00000000}},
	{{0x3EAAAAAB, 0x3F2AAAAB}, {0x40400000, 0x3FC00000}, {0xBF800000, 0xBF800000}},
};

static void RunPsFma(const TestDef* t)
{
	(void)t;
	BeginPs();
#define CALL_PS3(id, mn) \
	for (u32 i = 0; i < sizeof(kFmaPairs) / sizeof(kFmaPairs[0]); i++) \
	{ \
		ps3_##id(kFmaPairs[i][0], kFmaPairs[i][1], kFmaPairs[i][2]); \
		PutOut(2); \
	}
	PS_OPS3(CALL_PS3)
#undef CALL_PS3
	EndPs();
}

/* --- unary ops: insn frD, frB --- */

typedef void (*Ps1Fn)(const u32* b);

#define GEN_PS1(id, mn) \
	static NOINLINE void ps1_##id(const u32* b) \
	{ \
		__asm__ volatile( \
			"psq_l 1,0(%[b]),0,7\n\t" \
			mn " 2,1\n\t" \
			"stfd 2,0(%[o])\n\t" \
			"ps_merge10 3,2,2\n\t" \
			"stfd 3,8(%[o])" \
			: \
			: [b] "b"(b), [o] "b"(s_out) \
			: "fr1", "fr2", "fr3", "memory"); \
	}

#define PS_OPS1(X) \
	X(ps_neg, "ps_neg") X(ps_abs, "ps_abs") X(ps_nabs, "ps_nabs") X(ps_mr, "ps_mr") \
	X(ps_res, "ps_res") X(ps_rsqrte, "ps_rsqrte")

PS_OPS1(GEN_PS1)

static const u32 kPairsUnary[][2] __attribute__((aligned(8))) = {
	{0x40800000, 0x3E800000}, // 4, 0.25
	{0xBF800000, 0x000AE398}, // -1, 1e-39
	{0x40400000, 0x3DCCCCCD}, // 3, 0.1
	{0x00000001, 0x80800000}, // smallest denormal, -smallest normal
	{0x501502F9, 0x3F7FFFFF}, // 1e10, 1-ulp
};

static void RunPs1(const TestDef* t)
{
	Ps1Fn fn = (Ps1Fn)t->ctx;
	BeginPs();
	for (u32 i = 0; i < NUM_PAIRS; i++)
	{
		fn(kPairs[i]);
		PutOut(2);
	}
	for (u32 i = 0; i < sizeof(kPairsUnary) / sizeof(kPairsUnary[0]); i++)
	{
		fn(kPairsUnary[i]);
		PutOut(2);
	}
	EndPs();
}

/* --- compares --- */

#define GEN_PSCMP(id, insn, field) \
	static NOINLINE u32 pscmp_##id(const u32* a, const u32* b, u32* otherFields) \
	{ \
		u32 c; \
		__asm__ volatile( \
			"psq_l 1,0(%[a]),0,7\n\t" \
			"psq_l 2,0(%[b]),0,7\n\t" \
			"mtcrf 0xff,%[z]\n\t" \
			insn "\n\t" \
			"mfcr %[c]" \
			: [c] "=&r"(c) \
			: [a] "b"(a), [b] "b"(b), [z] "r"(0) \
			: "fr1", "fr2", "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7", "memory"); \
		*otherFields |= c & ~(0xF0000000u >> ((field) * 4)); \
		return (c >> (28 - (field) * 4)) & 0xF; \
	}

GEN_PSCMP(ps_cmpu0, "ps_cmpu0 cr0,1,2", 0)
GEN_PSCMP(ps_cmpo0, "ps_cmpo0 cr7,1,2", 7)
GEN_PSCMP(ps_cmpu1, "ps_cmpu1 cr1,1,2", 1)
GEN_PSCMP(ps_cmpo1, "ps_cmpo1 cr6,1,2", 6)

typedef u32 (*PsCmpFn)(const u32* a, const u32* b, u32* otherFields);

static void RunPsCmp(const TestDef* t)
{
	PsCmpFn fn = (PsCmpFn)t->ctx;
	BeginPs();
	u32 packed = 0, n = 0, other = 0;
	for (u32 i = 0; i < NUM_PAIRS; i++)
	{
		for (u32 j = 0; j < NUM_PAIRS; j++)
		{
			packed = (packed << 4) | fn(kPairs[i], kPairs[j], &other);
			if (++n == 8)
			{
				Put(packed);
				packed = 0;
				n = 0;
			}
		}
	}
	if (n)
		Put(packed);
	Put(other);
	EndPs();
}

/* --- quantized stores --- */

static u8 s_qbuf[64] __attribute__((aligned(32)));

static NOINLINE void PsqStore(const u32* pair, u8* dst)
{
	__asm__ volatile(
		"psq_l 1,0(%[s]),0,7\n\t"
		"psq_st 1,0(%[d]),0,6\n\t"
		"psq_st 1,8(%[d]),1,6"
		:
		: [s] "b"(pair), [d] "b"(dst)
		: "fr1", "memory");
}

static const u32 kStoreGqrs[] = {
	GQR(QT_FLOAT, 0, QT_FLOAT, 0),
	GQR(QT_U8, 0, QT_U8, 0),
	GQR(QT_U16, 0, QT_U16, 0),
	GQR(QT_S8, 0, QT_S8, 0),
	GQR(QT_S16, 0, QT_S16, 0),
	GQR(QT_U8, 7, QT_U8, 7),
	GQR(QT_S16, 8, QT_S16, 8),
	GQR(QT_S8, 63, QT_S8, 63),     // scale -1
	GQR(QT_U16, 60, QT_U16, 60),   // scale -4
	GQR(QT_S16, 32, QT_S16, 32),   // scale -32
	GQR(QT_U16, 31, QT_U16, 31),
	GQR(QT_FLOAT, 5, QT_FLOAT, 5), // scale is ignored for float
};
#define NUM_STORE_GQRS (sizeof(kStoreGqrs) / sizeof(kStoreGqrs[0]))

static const u32 kStoreVals[][2] __attribute__((aligned(8))) = {
	{0x3F000000, 0xBF800000}, // 0.5, -1.0
	{0x3FE00000, 0x43963333}, // 1.75, 300.4
	{0xC348999A, 0x4788B800}, // -200.6, 70000.0
	{0x42FF0000, 0xC3010000}, // 127.5, -129.0
	{0x437F8000, 0xBF000000}, // 255.5, -0.5
	{0x7FC00000, 0x7F800000}, // NaN, inf
	{0x80000000, 0x501502F9}, // -0.0, 1e10
	{0x46FFFECD, 0xC700009A}, // 32767.4, -32768.6
};
#define NUM_STORE_VALS (sizeof(kStoreVals) / sizeof(kStoreVals[0]))

static void RunPsqStore(const TestDef* t)
{
	(void)t;
	BeginPs();
	for (u32 g = 0; g < NUM_STORE_GQRS; g++)
	{
		SetGqr6(kStoreGqrs[g]);
		for (u32 v = 0; v < NUM_STORE_VALS; v++)
		{
			memset(s_qbuf, 0xAA, 16);
			PsqStore(kStoreVals[v], s_qbuf);
			for (u32 i = 0; i < 16; i += 4)
			{
				u32 w;
				memcpy(&w, s_qbuf + i, 4);
				Put(w);
			}
		}
	}
	EndPs();
}

/* --- quantized loads --- */

static NOINLINE void PsqLoad(const u8* src)
{
	__asm__ volatile(
		"psq_l 1,0(%[s]),0,6\n\t"
		"psq_l 2,0(%[s]),1,6\n\t"
		"stfd 1,0(%[o])\n\t"
		"ps_merge10 3,1,1\n\t"
		"stfd 3,8(%[o])\n\t"
		"stfd 2,16(%[o])\n\t"
		"ps_merge10 3,2,2\n\t"
		"stfd 3,24(%[o])"
		:
		: [s] "b"(src), [o] "b"(s_out)
		: "fr1", "fr2", "fr3", "memory");
}

static const u32 kLoadGqrs[] = {
	GQR(QT_FLOAT, 0, QT_FLOAT, 0),
	GQR(QT_U8, 0, QT_U8, 0),
	GQR(QT_U16, 0, QT_U16, 0),
	GQR(QT_S8, 0, QT_S8, 0),
	GQR(QT_S16, 0, QT_S16, 0),
	GQR(QT_U8, 1, QT_U8, 0),
	GQR(QT_S16, 7, QT_S16, 0),
	GQR(QT_S8, 63, QT_S8, 0),   // scale -1
	GQR(QT_U16, 40, QT_U16, 0), // scale -24
	GQR(QT_S16, 31, QT_S16, 0),
};
#define NUM_LOAD_GQRS (sizeof(kLoadGqrs) / sizeof(kLoadGqrs[0]))

static const u8 kLoadBytes[16] __attribute__((aligned(8))) = {
	0x80, 0x7F, 0xFF, 0x01, 0x00, 0x40, 0xC0, 0x12,
	0x3F, 0x80, 0x00, 0x00, 0xBF, 0xC0, 0x00, 0x00,
};

static void RunPsqLoad(const TestDef* t)
{
	(void)t;
	BeginPs();
	memcpy(s_qbuf, kLoadBytes, sizeof(kLoadBytes));
	for (u32 g = 0; g < NUM_LOAD_GQRS; g++)
	{
		SetGqr6(kLoadGqrs[g]);
		PsqLoad(s_qbuf + 0);
		PutOut(4);
		PsqLoad(s_qbuf + 8);
		PutOut(4);
		PsqLoad(s_qbuf + 2);
		PutOut(4);
	}
	EndPs();
}

/* --- psq addressing forms --- */

static NOINLINE void PsqAddressing(u8* base)
{
	u8* p = base;
	u32 o1, o2;
	__asm__ volatile(
		"psq_lx 1,%[p],%[i],0,7\n\t"
		"psq_lu 2,8(%[p]),0,7\n\t"
		"mr %[o1],%[p]\n\t"
		"psq_lux 3,%[p],%[i],1,7\n\t"
		"mr %[o2],%[p]\n\t"
		"psq_stx 1,%[p],%[i],0,7\n\t"
		"psq_stu 2,-4(%[p]),0,7\n\t"
		"psq_stux 3,%[p],%[i],0,7\n\t"
		"psq_l 4,-12(%[p]),0,7\n\t"
		"ps_merge01 4,4,1\n\t"
		"psq_st 4,4(%[p]),1,7"
		: [p] "+b"(p), [o1] "=&r"(o1), [o2] "=&r"(o2)
		: [i] "r"(8)
		: "fr1", "fr2", "fr3", "fr4", "memory");
	Put(o1 - (u32)base);
	Put(o2 - (u32)base);
	Put((u32)(p - base));
}

static void RunPsqAddressing(const TestDef* t)
{
	(void)t;
	BeginPs();
	for (u32 i = 0; i < 16; i++)
	{
		u32 v = 0x3F800000 + i * 0x00100000;
		memcpy(s_qbuf + i * 4, &v, 4);
	}
	PsqAddressing(s_qbuf + 8);
	for (u32 i = 0; i < 64; i += 4)
	{
		u32 w;
		memcpy(&w, s_qbuf + i, 4);
		Put(w);
	}
	EndPs();
}

/* --- scalar FP instructions and ps1 --- */

// In paired single mode lfs and single precision arithmetic also write ps1,
// while double precision ops leave ps1 alone. Each case starts from a register
// holding a known pair.
#define GEN_PSSCALAR(id, insn) \
	static NOINLINE void psscalar_##id(const u32* pair, const double* d) \
	{ \
		__asm__ volatile( \
			"psq_l 1,0(%[p]),0,7\n\t" \
			"psq_l 2,8(%[p]),0,7\n\t" \
			insn "\n\t" \
			"stfd 1,0(%[o])\n\t" \
			"ps_merge10 3,1,1\n\t" \
			"stfd 3,8(%[o])" \
			: \
			: [p] "b"(pair), [d] "b"(d), [o] "b"(s_out) \
			: "fr1", "fr2", "fr3", "memory"); \
	}

#define PS_SCALAR_OPS(X) \
	X(lfs, "lfs 1,4(%[d])") \
	X(lfd, "lfd 1,0(%[d])") \
	X(fadds, "fadds 1,2,2") \
	X(fadd, "fadd 1,2,2") \
	X(fmuls, "fmuls 1,1,2") \
	X(fmadds, "fmadds 1,1,2,2") \
	X(fdivs, "fdivs 1,2,1") \
	X(fres, "fres 1,2") \
	X(frsp, "lfd 1,0(%[d])\n\tfrsp 1,1") \
	X(fmr, "fmr 1,2") \
	X(fneg, "fneg 1,2") \
	X(fabs, "fabs 1,2") \
	X(fctiwz, "fctiwz 1,2") \
	X(fsel, "fsel 1,2,2,1")

PS_SCALAR_OPS(GEN_PSSCALAR)

static void RunPsScalar(const TestDef* t)
{
	typedef void (*Fn)(const u32*, const double*);
	static const u32 pairs[4] __attribute__((aligned(8))) = {0x3FC00000, 0xC0400000, 0x40A00000, 0x3E000000};
	static const double d[1] = {1.0000001234567};
	Fn fn = (Fn)t->ctx;
	BeginPs();
	fn(pairs, d);
	PutOut(2);
	EndPs();
}

/* --- pseudo random fuzzing --- */

#define PS_FUZZ_ITERATIONS 256

static u32 RandomFloatBits(u32* seed)
{
	u32 v = Lcg(seed);
	u32 sel = Lcg(seed);
	u32 sign = v & 0x80000000;
	u32 exp;
	switch (sel & 7)
	{
	case 0: exp = 127 + ((sel >> 8) & 7) - 3; break;
	case 1: exp = (sel >> 8) & 1; break;
	case 2: exp = 0xFE + ((sel >> 8) & 1); break;
	default: exp = (sel >> 8) & 0xFF; break;
	}
	return sign | (exp << 23) | (v & 0x7FFFFF);
}

static u32 s_fuzzPairs[3][2] __attribute__((aligned(8)));

static void HashOut(u32* h)
{
	for (u32 i = 0; i < 2; i++)
	{
		*h = Fnv1a(*h, (u32)(s_out[i] >> 32));
		*h = Fnv1a(*h, (u32)s_out[i]);
	}
}

static void RunPsFuzz2(const TestDef* t)
{
	Ps2Fn fn = (Ps2Fn)t->ctx;
	u32 seed = 0x5EED0010;
	u32 h = FNV_INIT;
	BeginPs();
	for (u32 i = 0; i < PS_FUZZ_ITERATIONS; i++)
	{
		for (u32 j = 0; j < 2; j++)
		{
			s_fuzzPairs[j][0] = RandomFloatBits(&seed);
			s_fuzzPairs[j][1] = RandomFloatBits(&seed);
		}
		fn(s_fuzzPairs[0], s_fuzzPairs[1]);
		HashOut(&h);
	}
	EndPs();
	Put(h);
}

static void RunPsFuzz3(const TestDef* t)
{
	Ps3Fn fn = (Ps3Fn)t->ctx;
	u32 seed = 0x5EED0011;
	u32 h = FNV_INIT;
	BeginPs();
	for (u32 i = 0; i < PS_FUZZ_ITERATIONS; i++)
	{
		for (u32 j = 0; j < 3; j++)
		{
			s_fuzzPairs[j][0] = RandomFloatBits(&seed);
			s_fuzzPairs[j][1] = RandomFloatBits(&seed);
		}
		fn(s_fuzzPairs[0], s_fuzzPairs[1], s_fuzzPairs[2]);
		HashOut(&h);
	}
	EndPs();
	Put(h);
}

static void RunPsFuzz1(const TestDef* t)
{
	Ps1Fn fn = (Ps1Fn)t->ctx;
	u32 seed = 0x5EED0012;
	u32 h = FNV_INIT;
	BeginPs();
	for (u32 i = 0; i < PS_FUZZ_ITERATIONS; i++)
	{
		s_fuzzPairs[0][0] = RandomFloatBits(&seed);
		s_fuzzPairs[0][1] = RandomFloatBits(&seed);
		fn(s_fuzzPairs[0]);
		HashOut(&h);
	}
	EndPs();
	Put(h);
}

// random quantization round trips through every GQR type/scale combination
static void RunPsqFuzz(const TestDef* t)
{
	u32 seed = 0x5EED0013;
	u32 h = FNV_INIT;
	(void)t;
	BeginPs();
	for (u32 i = 0; i < PS_FUZZ_ITERATIONS; i++)
	{
		u32 sel = Lcg(&seed);
		static const u32 types[] = {QT_FLOAT, QT_U8, QT_U16, QT_S8, QT_S16};
		u32 type = types[sel % 5];
		u32 scale = (sel >> 8) & 0x3F;
		SetGqr6(GQR(type, scale, type, scale));
		// values in a range where quantization is meaningful
		for (u32 j = 0; j < 2; j++)
		{
			u32 v = Lcg(&seed);
			u32 exp = 120 + ((v >> 24) & 15);
			s_fuzzPairs[0][j] = (v & 0x80000000) | (exp << 23) | (v & 0x7FFFFF);
		}
		memset(s_qbuf, 0, 16);
		PsqStore(s_fuzzPairs[0], s_qbuf);
		for (u32 k = 0; k < 16; k += 4)
		{
			u32 w;
			memcpy(&w, s_qbuf + k, 4);
			h = Fnv1a(h, w);
		}
		PsqLoad(s_qbuf);
		for (u32 k = 0; k < 4; k++)
		{
			h = Fnv1a(h, (u32)(s_out[k] >> 32));
			h = Fnv1a(h, (u32)s_out[k]);
		}
	}
	EndPs();
	Put(h);
}

#define TAB_PS2(id, mn) {mn, RunPs2, (const void*)ps2_##id},
#define TAB_PS3(id, mn) {mn, RunPs3, (const void*)ps3_##id},
#define TAB_PS1(id, mn) {mn, RunPs1, (const void*)ps1_##id},
#define TAB_PSSCALAR(id, insn) {"ps1_after_" #id, RunPsScalar, (const void*)psscalar_##id},
#define TAB_PSFUZZ2(id, mn) {"fuzz." mn, RunPsFuzz2, (const void*)ps2_##id},
#define TAB_PSFUZZ3(id, mn) {"fuzz." mn, RunPsFuzz3, (const void*)ps3_##id},
#define TAB_PSFUZZ1(id, mn) {"fuzz." mn, RunPsFuzz1, (const void*)ps1_##id},

const TestDef g_psTests[] = {
	PS_OPS2(TAB_PS2)
	PS_OPS3(TAB_PS3)
	{"ps_fma_precision", RunPsFma, 0},
	PS_OPS1(TAB_PS1)
	{"ps_cmpu0", RunPsCmp, (const void*)pscmp_ps_cmpu0},
	{"ps_cmpo0", RunPsCmp, (const void*)pscmp_ps_cmpo0},
	{"ps_cmpu1", RunPsCmp, (const void*)pscmp_ps_cmpu1},
	{"ps_cmpo1", RunPsCmp, (const void*)pscmp_ps_cmpo1},
	{"psq_st_quant", RunPsqStore, 0},
	{"psq_l_quant", RunPsqLoad, 0},
	{"psq_addressing", RunPsqAddressing, 0},
	PS_SCALAR_OPS(TAB_PSSCALAR)
	PS_OPS2(TAB_PSFUZZ2)
	PS_OPS3(TAB_PSFUZZ3)
	PS_OPS1(TAB_PSFUZZ1)
	{"fuzz.psq_quant", RunPsqFuzz, 0},
};

const u32 g_psTestCount = sizeof(g_psTests) / sizeof(g_psTests[0]);
