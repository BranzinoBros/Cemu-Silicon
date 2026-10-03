// FPU tests: double and single arithmetic, fused multiply-add, estimates,
// conversions, fsel/fabs/fneg, compares, NaN/infinity/denormal inputs,
// rounding modes and FPSCR.
//
// Note: Cemu's interpreter does not implement mtfsfi, mtfsb0 or mcrfs (they
// would trigger an assert in debug builds), so FPSCR is only written via mtfsf.

#include "tests.h"

static const u64 kFpVals[] = {
	0x0000000000000000ull, // +0
	0x8000000000000000ull, // -0
	0x3FF0000000000000ull, // 1
	0xBFF8000000000000ull, // -1.5
	0x400921FB54442D18ull, // pi
	0x3FF0000000000001ull, // 1 + ulp
	0x7E37E43C8800759Cull, // 1e300
	0x000FFFFFFFFFFFFFull, // largest denormal
	0x7FF0000000000000ull, // +inf
	0xFFF0000000000000ull, // -inf
	0x7FF8000000000ABCull, // qNaN with payload
	0xFFF0000000000DEFull, // negative sNaN with payload
};
#define NUM_FP_VALS (sizeof(kFpVals) / sizeof(kFpVals[0]))

static const u64 kFpUnaryVals[] = {
	0x0000000000000000ull, // +0
	0x8000000000000000ull, // -0
	0x3FF0000000000000ull, // 1
	0xBFF0000000000000ull, // -1
	0x3FE0000000000000ull, // 0.5
	0xBFE0000000000000ull, // -0.5
	0x3FF8000000000000ull, // 1.5
	0xBFF8000000000000ull, // -1.5
	0x4004000000000000ull, // 2.5
	0xC004000000000000ull, // -2.5
	0x4008000000000000ull, // 3
	0x3FB999999999999Aull, // 0.1
	0x3FDFFFFFFFFFFFFFull, // 0.49999999999999994
	0x41DFFFFFFFE00000ull, // 2147483647.5
	0x41E0000000000000ull, // 2147483648
	0xC1E0000000200000ull, // -2147483649
	0x3FF0000010000000ull, // 1 + 2^-24
	0x3FF0000010000001ull, // 1 + 2^-24 + 2^-52
	0x47EFFFFFF0000000ull, // just above max float, rounds to inf
	0x4812A2A2A2A2A2A2ull, // ~1e39
	0x3800000000000000ull, // 2^-127 (single denormal)
	0x3680000000000000ull, // 2^-151 (single underflow)
	0x0000000000000001ull, // double denormal min
	0x7E37E43C8800759Cull, // 1e300
	0x7FF0000000000000ull, // +inf
	0xFFF0000000000000ull, // -inf
	0x7FF8000000000ABCull, // qNaN payload
	0x7FF00000200000DEull, // sNaN with payload in high bits
	0xC010000000000000ull, // -4
};
#define NUM_FP_UNARY_VALS (sizeof(kFpUnaryVals) / sizeof(kFpUnaryVals[0]))

/* --- two operand arithmetic --- */

typedef double (*FpOp2Fn)(double a, double b);

#define GEN_FOP2(id, mn) \
	static NOINLINE double fop2_##id(double a, double b) \
	{ \
		double r; \
		__asm__ volatile(mn " %[r],%[a],%[b]" : [r] "=f"(r) : [a] "f"(a), [b] "f"(b)); \
		return r; \
	}

#define FP_OPS2(X) \
	X(fadd, "fadd") X(fsub, "fsub") X(fmul, "fmul") X(fdiv, "fdiv") \
	X(fadds, "fadds") X(fsubs, "fsubs") X(fmuls, "fmuls") X(fdivs, "fdivs")

FP_OPS2(GEN_FOP2)

static void RunFpOp2(const TestDef* t)
{
	FpOp2Fn fn = (FpOp2Fn)t->ctx;
	for (u32 i = 0; i < NUM_FP_VALS; i++)
		for (u32 j = 0; j < NUM_FP_VALS; j++)
			PutD(fn(BitsDouble(kFpVals[i]), BitsDouble(kFpVals[j])));
}

/* --- fused multiply-add: frD = +-(frA * frC +- frB) --- */

typedef double (*FpOp3Fn)(double a, double c, double b);

#define GEN_FOP3(id, mn) \
	static NOINLINE double fop3_##id(double a, double c, double b) \
	{ \
		double r; \
		__asm__ volatile(mn " %[r],%[a],%[c],%[b]" : [r] "=f"(r) : [a] "f"(a), [c] "f"(c), [b] "f"(b)); \
		return r; \
	}

#define FP_OPS3(X) \
	X(fmadd, "fmadd") X(fmsub, "fmsub") X(fnmadd, "fnmadd") X(fnmsub, "fnmsub") \
	X(fmadds, "fmadds") X(fmsubs, "fmsubs") X(fnmadds, "fnmadds") X(fnmsubs, "fnmsubs")

FP_OPS3(GEN_FOP3)

// a, c, b
static const u64 kFmaTriples[][3] = {
	{0x3FF0000008000000ull, 0x3FEFFFFFF0000000ull, 0xBFF0000000000000ull}, // (1+2^-27)(1-2^-27)-1, fused != unfused
	{0x400921FB54442D18ull, 0x400921FB54442D18ull, 0xC023BD3CC9BE45DEull}, // pi*pi - round(pi*pi)
	{0x4008000000000000ull, 0x3FD5555555555555ull, 0xBFF0000000000000ull}, // 3 * (1/3) - 1
	{0x3FB999999999999Aull, 0x4024000000000000ull, 0xBFF0000000000000ull}, // 0.1 * 10 - 1
	{0x3FF0000800000000ull, 0x3FF0000800000000ull, 0xBFF0000000000000ull}, // (1+2^-12)^2 - 1 (single fused)
	{0x7FF0000000000000ull, 0x0000000000000000ull, 0x3FF0000000000000ull}, // inf * 0 + 1
	{0x7FF0000000000000ull, 0x3FF0000000000000ull, 0xFFF0000000000000ull}, // inf - inf
	{0x7FF8000000000AAAull, 0x7FF8000000000CCCull, 0x7FF8000000000BBBull}, // NaN priority: A
	{0x3FF0000000000000ull, 0x7FF8000000000CCCull, 0x7FF8000000000BBBull}, // NaN priority: B
	{0x3FF0000000000000ull, 0x7FF8000000000CCCull, 0x3FF0000000000000ull}, // NaN priority: C
	{0x7FF0000000000AAAull, 0x3FF0000000000000ull, 0x3FF0000000000000ull}, // sNaN in A
	{0x3FF0000000000000ull, 0x3FF0000000000000ull, 0xFFF0000000000BBBull}, // sNaN in B
	{0x7E37E43C8800759Cull, 0x7E37E43C8800759Cull, 0xFFF0000000000000ull}, // huge*huge - inf
	{0x7E37E43C8800759Cull, 0x7E37E43C8800759Cull, 0x3FF0000000000000ull}, // overflow
	{0x8000000000000000ull, 0x3FF0000000000000ull, 0x8000000000000000ull}, // -0 * 1 + -0
	{0x0000000000000000ull, 0x3FF0000000000000ull, 0x8000000000000000ull}, // +0 * 1 + -0
	{0x000FFFFFFFFFFFFFull, 0x4330000000000000ull, 0x0000000000000000ull}, // denormal * 2^52
	{0x3FF8000000000000ull, 0x0000000000000800ull, 0x0000000000000800ull}, // denormal result
	{0x47EFFFFFE0000000ull, 0x4000000000000000ull, 0xC7EFFFFFE0000000ull}, // max float * 2 - max float
	{0x3FF5555555555555ull, 0x3FF5555555555555ull, 0x3FF5555555555555ull}, // non single-representable inputs
	{0x380FFFFFE0000000ull, 0x3FF0000000000000ull, 0x0000000000000000ull}, // single denormal range
};
#define NUM_FMA_TRIPLES (sizeof(kFmaTriples) / sizeof(kFmaTriples[0]))

static void RunFpOp3(const TestDef* t)
{
	FpOp3Fn fn = (FpOp3Fn)t->ctx;
	for (u32 i = 0; i < NUM_FMA_TRIPLES; i++)
		PutD(fn(BitsDouble(kFmaTriples[i][0]), BitsDouble(kFmaTriples[i][1]), BitsDouble(kFmaTriples[i][2])));
	// all combinations of a small value set
	static const u32 sel[] = {0, 2, 3, 6, 7, 8, 10};
	for (u32 i = 0; i < 7; i++)
		for (u32 j = 0; j < 7; j++)
			PutD(fn(BitsDouble(kFpVals[sel[i]]), BitsDouble(kFpVals[sel[j]]), BitsDouble(kFpVals[sel[(i + j) % 7]])));
}

/* --- one operand ops --- */

typedef double (*FpOp1Fn)(double b);

#define GEN_FOP1(id, mn) \
	static NOINLINE double fop1_##id(double b) \
	{ \
		double r; \
		__asm__ volatile(mn " %[r],%[b]" : [r] "=f"(r) : [b] "f"(b)); \
		return r; \
	}

#define FP_OPS1(X) \
	X(fabs, "fabs") X(fnabs, "fnabs") X(fneg, "fneg") X(fmr, "fmr") \
	X(frsp, "frsp") X(fres, "fres") X(frsqrte, "frsqrte")

#define FP_CONV_OPS(X) X(fctiw, "fctiw") X(fctiwz, "fctiwz")

FP_OPS1(GEN_FOP1)
FP_CONV_OPS(GEN_FOP1)

static void RunFpOp1(const TestDef* t)
{
	FpOp1Fn fn = (FpOp1Fn)t->ctx;
	for (u32 i = 0; i < NUM_FP_UNARY_VALS; i++)
		PutD(fn(BitsDouble(kFpUnaryVals[i])));
}

// the integer result is in the low word (this is what stfiwx stores)
static void RunFpConv(const TestDef* t)
{
	FpOp1Fn fn = (FpOp1Fn)t->ctx;
	for (u32 i = 0; i < NUM_FP_UNARY_VALS; i++)
		Put((u32)DoubleBits(fn(BitsDouble(kFpUnaryVals[i]))));
}

// the high word is undefined on some PowerPC implementations, kept separate so
// a mismatch here can be judged on its own
static void RunFpConvHi(const TestDef* t)
{
	FpOp1Fn fn = (FpOp1Fn)t->ctx;
	for (u32 i = 0; i < NUM_FP_UNARY_VALS; i++)
		Put((u32)(DoubleBits(fn(BitsDouble(kFpUnaryVals[i]))) >> 32));
}

/* --- fsel --- */

static NOINLINE double Fsel(double a, double c, double b)
{
	double r;
	__asm__ volatile("fsel %[r],%[a],%[c],%[b]" : [r] "=f"(r) : [a] "f"(a), [c] "f"(c), [b] "f"(b));
	return r;
}

static void RunFsel(const TestDef* t)
{
	(void)t;
	for (u32 i = 0; i < NUM_FP_VALS; i++)
		PutD(Fsel(BitsDouble(kFpVals[i]), 2.0, 3.0));
	for (u32 i = 0; i < NUM_FP_VALS; i++)
		PutD(Fsel(-1.0, BitsDouble(kFpVals[i]), BitsDouble(kFpVals[(i + 3) % NUM_FP_VALS])));
}

/* --- compares --- */

#define GEN_FCMP(id, insn, field) \
	static NOINLINE u32 fcmp_##id(double a, double b, u32 xin, u32* otherFields) \
	{ \
		u32 c; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0xff,%[z]\n\t" \
			insn "\n\t" \
			"mfcr %[c]" \
			: [c] "=&r"(c) \
			: [a] "f"(a), [b] "f"(b), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7", "xer"); \
		*otherFields |= c & ~(0xF0000000u >> ((field) * 4)); \
		return (c >> (28 - (field) * 4)) & 0xF; \
	}

GEN_FCMP(fcmpu_cr0, "fcmpu cr0,%[a],%[b]", 0)
GEN_FCMP(fcmpu_cr6, "fcmpu cr6,%[a],%[b]", 6)
GEN_FCMP(fcmpo_cr1, "fcmpo cr1,%[a],%[b]", 1)

typedef u32 (*FcmpFn)(double a, double b, u32 xin, u32* otherFields);

// packs the target CR field, 8 compares per word. The last word holds any bits
// that were set in other CR fields (should be zero)
static void RunFcmp(const TestDef* t)
{
	FcmpFn fn = (FcmpFn)t->ctx;
	u32 packed = 0, n = 0, other = 0;
	for (u32 i = 0; i < NUM_FP_VALS; i++)
	{
		for (u32 j = 0; j < NUM_FP_VALS; j++)
		{
			// XER[SO] is not copied for FP compares, set it to verify
			packed = (packed << 4) | fn(BitsDouble(kFpVals[i]), BitsDouble(kFpVals[j]), 0x80000000, &other);
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
}

/* --- FPSCR: rounding modes --- */

static NOINLINE void SetFpscr(u32 v)
{
	double d = BitsDouble((u64)v);
	__asm__ volatile("mtfsf 0xff,%[d]" : : [d] "f"(d));
}

static NOINLINE u32 GetFpscr(void)
{
	double d;
	__asm__ volatile("mffs %[d]" : [d] "=f"(d));
	return (u32)DoubleBits(d);
}

static void RunRounding(const TestDef* t)
{
	(void)t;
	u32 saved = GetFpscr();
	for (u32 rn = 0; rn < 4; rn++)
	{
		SetFpscr(rn);
		Put(GetFpscr() & 0xFF);
		Put((u32)DoubleBits(fop1_fctiw(2.5)));
		Put((u32)DoubleBits(fop1_fctiw(-2.5)));
		Put((u32)DoubleBits(fop1_fctiw(1.5)));
		Put((u32)DoubleBits(fop1_fctiw(-0.5)));
		Put((u32)DoubleBits(fop1_fctiwz(-1.7)));
		PutD(fop2_fadd(1.0, BitsDouble(0x3CA0000000000001ull)));  // 1 + (2^-53 + tiny)
		PutD(fop2_fadd(-1.0, BitsDouble(0xBCA0000000000001ull)));
		PutD(fop2_fadds(1.0, BitsDouble(0x3E70000000000001ull))); // 1 + (2^-24 + tiny)
		PutD(fop2_fdiv(1.0, 3.0));
		PutD(fop2_fdivs(-1.0, 3.0));
		PutD(fop1_frsp(BitsDouble(0x3FF0000010000000ull)));
		PutD(fop1_frsp(BitsDouble(0xBFF0000030000000ull)));
		PutD(fop3_fmadd(BitsDouble(0x3FF0000008000000ull), BitsDouble(0x3FEFFFFFF0000000ull), -1.0));
	}
	SetFpscr(saved);
}

// mtfsf with partial field masks, read back via mffs
static NOINLINE u32 MtfsfMask(u32 base, u32 v, u32 maskSel)
{
	double db = BitsDouble((u64)base);
	double dv = BitsDouble((u64)v);
	double r;
	switch (maskSel)
	{
	case 0:
		__asm__ volatile("mtfsf 0xff,%[b]\n\tmtfsf 0x01,%[v]\n\tmffs %[r]" : [r] "=f"(r) : [b] "f"(db), [v] "f"(dv));
		break;
	case 1:
		__asm__ volatile("mtfsf 0xff,%[b]\n\tmtfsf 0x80,%[v]\n\tmffs %[r]" : [r] "=f"(r) : [b] "f"(db), [v] "f"(dv));
		break;
	default:
		__asm__ volatile("mtfsf 0xff,%[b]\n\tmtfsf 0x3c,%[v]\n\tmffs %[r]" : [r] "=f"(r) : [b] "f"(db), [v] "f"(dv));
		break;
	}
	return (u32)DoubleBits(r);
}

static void RunMtfsf(const TestDef* t)
{
	(void)t;
	u32 saved = GetFpscr();
	// enable bits (VE/OE/UE/ZE/XE, bits 24-28) are never set to avoid program exceptions
	static const u32 vals[] = {0x00000000, 0x00000003, 0x00000005, 0x9FF00000, 0x0001F000};
	for (u32 i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
		for (u32 m = 0; m < 3; m++)
			Put(MtfsfMask(0x00000002, vals[i], m));
	SetFpscr(saved);
}

/* --- FPSCR: status flags after operations --- */

#define GEN_FPSCR_OP2(id, insn, va, vb) \
	static NOINLINE void fpscr_##id(void) \
	{ \
		double r, f; \
		double z = 0.0; \
		__asm__ volatile( \
			"mtfsf 0xff,%[z]\n\t" \
			insn "\n\t" \
			"mffs %[f]" \
			: [r] "=&f"(r), [f] "=&f"(f) \
			: [a] "f"(BitsDouble(va)), [b] "f"(BitsDouble(vb)), [z] "f"(z) \
			: "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7"); \
		PutD(r); \
		Put((u32)DoubleBits(f)); \
	}

#define FPSCR_OPS(X) \
	X(div_0_0, "fdiv %[r],%[a],%[b]", 0x0000000000000000ull, 0x0000000000000000ull) \
	X(div_1_0, "fdiv %[r],%[a],%[b]", 0x3FF0000000000000ull, 0x0000000000000000ull) \
	X(sub_inf_inf, "fsub %[r],%[a],%[b]", 0x7FF0000000000000ull, 0x7FF0000000000000ull) \
	X(mul_overflow, "fmul %[r],%[a],%[b]", 0x7E37E43C8800759Cull, 0x7E37E43C8800759Cull) \
	X(mul_underflow, "fmul %[r],%[a],%[b]", 0x0010000000000000ull, 0x3E70000000000000ull) \
	X(div_inexact, "fdiv %[r],%[a],%[b]", 0x3FF0000000000000ull, 0x4008000000000000ull) \
	X(add_exact, "fadd %[r],%[a],%[b]", 0x3FF0000000000000ull, 0x3FF0000000000000ull) \
	X(add_neg, "fadd %[r],%[a],%[b]", 0xBFF0000000000000ull, 0xBFF0000000000000ull) \
	X(add_zero, "fsub %[r],%[a],%[b]", 0x3FF0000000000000ull, 0x3FF0000000000000ull) \
	X(add_snan, "fadd %[r],%[a],%[b]", 0x7FF0000000000001ull, 0x3FF0000000000000ull) \
	X(adds_denorm, "fadds %[r],%[a],%[b]", 0x3800000000000000ull, 0x0000000000000000ull) \
	X(fcmpu_nan, "fcmpu cr1,%[a],%[b]\n\tfmr %[r],%[a]", 0x7FF8000000000000ull, 0x3FF0000000000000ull) \
	X(fcmpu_snan, "fcmpu cr1,%[a],%[b]\n\tfmr %[r],%[a]", 0x7FF0000000000001ull, 0x3FF0000000000000ull) \
	X(fcmpo_nan, "fcmpo cr1,%[a],%[b]\n\tfmr %[r],%[a]", 0x7FF8000000000000ull, 0x3FF0000000000000ull) \
	X(fcmpu_lt, "fcmpu cr1,%[a],%[b]\n\tfmr %[r],%[a]", 0x3FF0000000000000ull, 0x4000000000000000ull) \
	X(fctiw_nan, "fctiw %[r],%[a]", 0x7FF8000000000000ull, 0) \
	X(fctiw_big, "fctiw %[r],%[a]", 0x7E37E43C8800759Cull, 0) \
	X(fctiwz_frac, "fctiwz %[r],%[a]", 0x3FF8000000000000ull, 0) \
	X(frsp_overflow, "frsp %[r],%[a]", 0x7E37E43C8800759Cull, 0) \
	X(fres_zero, "fres %[r],%[a]", 0x0000000000000000ull, 0) \
	X(frsqrte_neg, "frsqrte %[r],%[a]", 0xBFF0000000000000ull, 0) \
	X(fmadd_inf0, "fmadd %[r],%[a],%[b],%[a]", 0x7FF0000000000000ull, 0x0000000000000000ull)

FPSCR_OPS(GEN_FPSCR_OP2)

static void RunFpscrFlags(const TestDef* t)
{
	(void)t;
	u32 saved = GetFpscr();
#define CALL_FPSCR(id, insn, va, vb) fpscr_##id();
	FPSCR_OPS(CALL_FPSCR)
#undef CALL_FPSCR
	SetFpscr(saved);
}

/* --- pseudo random fuzzing --- */

#define FP_FUZZ_ITERATIONS 512

static double RandomDouble(u32* seed)
{
	u32 hi = Lcg(seed);
	u32 lo = Lcg(seed);
	u32 sel = Lcg(seed);
	u32 sign = hi & 0x80000000;
	u32 exp;
	switch (sel & 7)
	{
	case 0: exp = 0x3FF + ((sel >> 8) & 7) - 3; break;   // around 1
	case 1: exp = 0x380 + ((sel >> 8) & 0xFF); break;    // single range
	case 2: exp = 0x380 + ((sel >> 8) & 0xFF); lo &= 0xE0000000; break; // single representable
	case 3: exp = 0x380 + ((sel >> 8) & 0xFF); lo = 0; hi &= ~0x1FFFFF; break; // short mantissa
	case 4: exp = (sel >> 8) & 1; break;                 // denormal or tiny
	case 5: exp = 0x7FE + ((sel >> 8) & 1); break;       // huge, inf or NaN
	default: exp = (hi >> 20) & 0x7FF; break;            // anything
	}
	hi = sign | (exp << 20) | (hi & 0x000FFFFF);
	if ((sel & 0x700) == 0x700 && exp == 0x7FF)
		hi |= 0x00080000; // mostly quiet NaNs
	return BitsDouble(((u64)hi << 32) | lo);
}

static void RunFpFuzz2(const TestDef* t)
{
	FpOp2Fn fn = (FpOp2Fn)t->ctx;
	u32 seed = 0x5EED0001;
	u32 h = FNV_INIT;
	for (u32 i = 0; i < FP_FUZZ_ITERATIONS; i++)
	{
		double a = RandomDouble(&seed);
		double b = RandomDouble(&seed);
		u64 r = DoubleBits(fn(a, b));
		h = Fnv1a(h, (u32)(r >> 32));
		h = Fnv1a(h, (u32)r);
	}
	Put(h);
}

static void RunFpFuzz3(const TestDef* t)
{
	FpOp3Fn fn = (FpOp3Fn)t->ctx;
	u32 seed = 0x5EED0002;
	u32 h = FNV_INIT;
	for (u32 i = 0; i < FP_FUZZ_ITERATIONS; i++)
	{
		double a = RandomDouble(&seed);
		double c = RandomDouble(&seed);
		double b = RandomDouble(&seed);
		u64 r = DoubleBits(fn(a, c, b));
		h = Fnv1a(h, (u32)(r >> 32));
		h = Fnv1a(h, (u32)r);
	}
	Put(h);
}

static void RunFpFuzz1(const TestDef* t)
{
	FpOp1Fn fn = (FpOp1Fn)t->ctx;
	u32 seed = 0x5EED0003;
	u32 h = FNV_INIT;
	for (u32 i = 0; i < FP_FUZZ_ITERATIONS; i++)
	{
		u64 r = DoubleBits(fn(RandomDouble(&seed)));
		h = Fnv1a(h, (u32)(r >> 32));
		h = Fnv1a(h, (u32)r);
	}
	Put(h);
}

#define TAB_FOP2(id, mn) {mn, RunFpOp2, (const void*)fop2_##id},
#define TAB_FOP3(id, mn) {mn, RunFpOp3, (const void*)fop3_##id},
#define TAB_FOP1(id, mn) {mn, RunFpOp1, (const void*)fop1_##id},
#define TAB_FCONV(id, mn) {mn, RunFpConv, (const void*)fop1_##id},
#define TAB_FCONVHI(id, mn) {mn ".hi", RunFpConvHi, (const void*)fop1_##id},
#define TAB_FFUZZ2(id, mn) {"fuzz." mn, RunFpFuzz2, (const void*)fop2_##id},
#define TAB_FFUZZ3(id, mn) {"fuzz." mn, RunFpFuzz3, (const void*)fop3_##id},
#define TAB_FFUZZ1(id, mn) {"fuzz." mn, RunFpFuzz1, (const void*)fop1_##id},

const TestDef g_fpuTests[] = {
	FP_OPS2(TAB_FOP2)
	FP_OPS3(TAB_FOP3)
	FP_OPS1(TAB_FOP1)
	FP_CONV_OPS(TAB_FCONV)
	FP_CONV_OPS(TAB_FCONVHI)
	{"fsel", RunFsel, 0},
	{"fcmpu_cr0", RunFcmp, (const void*)fcmp_fcmpu_cr0},
	{"fcmpu_cr6", RunFcmp, (const void*)fcmp_fcmpu_cr6},
	{"fcmpo_cr1", RunFcmp, (const void*)fcmp_fcmpo_cr1},
	{"rounding_modes", RunRounding, 0},
	{"mtfsf_mffs", RunMtfsf, 0},
	{"fpscr_flags", RunFpscrFlags, 0},
	FP_OPS2(TAB_FFUZZ2)
	FP_OPS3(TAB_FFUZZ3)
	FP_OPS1(TAB_FFUZZ1)
	FP_CONV_OPS(TAB_FFUZZ1)
};

const u32 g_fpuTestCount = sizeof(g_fpuTests) / sizeof(g_fpuTests[0]);
