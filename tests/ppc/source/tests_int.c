// Integer unit tests: arithmetic with XER/CR effects, logical ops, rotates,
// shifts, compares and CR/XER manipulation.

#include "tests.h"

#define XER_SO 0x80000000u
#define XER_OV 0x40000000u
#define XER_CA 0x20000000u

static const u32 kVals[] = {
	0x00000000, 0x00000001, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x9E3779B9,
};
#define NUM_VALS (sizeof(kVals) / sizeof(kVals[0]))

static const u32 kValsExt[] = {
	0x00000000, 0x00000001, 0x00000002, 0x0000007F, 0x00000080, 0x00007FFF, 0x00008000, 0x0000FFFF,
	0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF, 0x9E3779B9, 0x00F0FF00,
};
#define NUM_VALS_EXT (sizeof(kValsExt) / sizeof(kValsExt[0]))

static const u32 kShifts[] = {
	0, 1, 4, 15, 31, 32, 33, 63, 64, 0xFFFFFFFF, 0x80000021,
};
#define NUM_SHIFTS (sizeof(kShifts) / sizeof(kShifts[0]))

// result + flags word: XER[SO,OV,CA] in the top bits, CR0 in the low nibble
static inline u32 Flags(u32 xer, u32 cr)
{
	return (xer & 0xE0000000) | (cr >> 28);
}

/* --- XO-form two operand ops: insn rD, rA, rB --- */

typedef void (*IntOp2Fn)(u32 a, u32 b, u32 xin, u32* res, u32* flg);

#define GEN_OP2(id, mn) \
	static NOINLINE void op2_##id(u32 a, u32 b, u32 xin, u32* res, u32* flg) \
	{ \
		u32 r, x, c; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0x80,%[z]\n\t" \
			mn " %[r],%[a],%[b]\n\t" \
			"mfxer %[x]\n\t" \
			"mfcr %[c]" \
			: [r] "=&r"(r), [x] "=&r"(x), [c] "=&r"(c) \
			: [a] "r"(a), [b] "r"(b), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "xer"); \
		*res = r; \
		*flg = Flags(x, c); \
	}

#define ARITH_OPS2(X) \
	X(add, "add") X(add_, "add.") X(addo, "addo") X(addo_, "addo.") \
	X(addc, "addc") X(addc_, "addc.") X(addco, "addco") X(addco_, "addco.") \
	X(adde, "adde") X(adde_, "adde.") X(addeo, "addeo") X(addeo_, "addeo.") \
	X(subf, "subf") X(subf_, "subf.") X(subfo, "subfo") X(subfo_, "subfo.") \
	X(subfc, "subfc") X(subfc_, "subfc.") X(subfco, "subfco") X(subfco_, "subfco.") \
	X(subfe, "subfe") X(subfe_, "subfe.") X(subfeo, "subfeo") X(subfeo_, "subfeo.") \
	X(mullw, "mullw") X(mullw_, "mullw.") X(mullwo, "mullwo") X(mullwo_, "mullwo.") \
	X(mulhw, "mulhw") X(mulhw_, "mulhw.") X(mulhwu, "mulhwu") X(mulhwu_, "mulhwu.") \
	X(divw, "divw") X(divw_, "divw.") X(divwo, "divwo") X(divwo_, "divwo.") \
	X(divwu, "divwu") X(divwu_, "divwu.") X(divwuo, "divwuo") X(divwuo_, "divwuo.")

#define LOGIC_OPS2(X) \
	X(and, "and") X(and_, "and.") X(andc, "andc") X(andc_, "andc.") \
	X(or, "or") X(or_, "or.") X(orc, "orc") X(orc_, "orc.") \
	X(xor, "xor") X(xor_, "xor.") X(nand, "nand") X(nand_, "nand.") \
	X(nor, "nor") X(nor_, "nor.") X(eqv, "eqv") X(eqv_, "eqv.")

#define SHIFT_OPS2(X) \
	X(slw, "slw") X(slw_, "slw.") X(srw, "srw") X(srw_, "srw.") \
	X(sraw, "sraw") X(sraw_, "sraw.")

ARITH_OPS2(GEN_OP2)
LOGIC_OPS2(GEN_OP2)
SHIFT_OPS2(GEN_OP2)

// all value pairs, once with XER clear and once with SO and CA set
static void RunArith2(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	static const u32 xins[] = {0, XER_SO | XER_CA};
	for (u32 x = 0; x < 2; x++)
	{
		for (u32 i = 0; i < NUM_VALS; i++)
		{
			for (u32 j = 0; j < NUM_VALS; j++)
			{
				u32 r, f;
				fn(kVals[i], kVals[j], xins[x], &r, &f);
				Put(r);
				Put(f);
			}
		}
	}
}

static void RunLogic2(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	for (u32 i = 0; i < NUM_VALS; i++)
	{
		for (u32 j = 0; j < NUM_VALS; j++)
		{
			u32 r, f;
			fn(kVals[i], kVals[j], XER_SO | XER_CA, &r, &f);
			Put(r);
			Put(f);
		}
	}
}

// shift amounts include values >= 32 (slw/srw/sraw use 6 bits of rB)
static void RunShift2(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	for (u32 i = 0; i < NUM_VALS; i++)
	{
		for (u32 j = 0; j < NUM_SHIFTS; j++)
		{
			u32 r, f;
			fn(kVals[i], kShifts[j], XER_CA, &r, &f);
			Put(r);
			Put(f);
		}
	}
}

/* --- one operand ops: insn rD, rA --- */

#define GEN_OP1(id, mn) \
	static NOINLINE void op1_##id(u32 a, u32 b, u32 xin, u32* res, u32* flg) \
	{ \
		u32 r, x, c; \
		(void)b; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0x80,%[z]\n\t" \
			mn " %[r],%[a]\n\t" \
			"mfxer %[x]\n\t" \
			"mfcr %[c]" \
			: [r] "=&r"(r), [x] "=&r"(x), [c] "=&r"(c) \
			: [a] "r"(a), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "xer"); \
		*res = r; \
		*flg = Flags(x, c); \
	}

#define OPS1(X) \
	X(addme, "addme") X(addme_, "addme.") X(addmeo, "addmeo") X(addmeo_, "addmeo.") \
	X(addze, "addze") X(addze_, "addze.") X(addzeo, "addzeo") X(addzeo_, "addzeo.") \
	X(subfme, "subfme") X(subfme_, "subfme.") X(subfmeo, "subfmeo") X(subfmeo_, "subfmeo.") \
	X(subfze, "subfze") X(subfze_, "subfze.") X(subfzeo, "subfzeo") X(subfzeo_, "subfzeo.") \
	X(neg, "neg") X(neg_, "neg.") X(nego, "nego") X(nego_, "nego.") \
	X(cntlzw, "cntlzw") X(cntlzw_, "cntlzw.") \
	X(extsb, "extsb") X(extsb_, "extsb.") X(extsh, "extsh") X(extsh_, "extsh.")

OPS1(GEN_OP1)

static void RunOp1(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	static const u32 xins[] = {0, XER_SO | XER_CA};
	for (u32 x = 0; x < 2; x++)
	{
		for (u32 i = 0; i < NUM_VALS_EXT; i++)
		{
			u32 r, f;
			fn(kValsExt[i], 0, xins[x], &r, &f);
			Put(r);
			Put(f);
		}
	}
}

/* --- immediate forms: insn rD, rA, imm --- */

#define GEN_OPI(id, mn, imm) \
	static NOINLINE void opi_##id(u32 a, u32 b, u32 xin, u32* res, u32* flg) \
	{ \
		u32 r, x, c; \
		(void)b; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0x80,%[z]\n\t" \
			mn " %[r],%[a]," #imm "\n\t" \
			"mfxer %[x]\n\t" \
			"mfcr %[c]" \
			: [r] "=&r"(r), [x] "=&r"(x), [c] "=&r"(c) \
			: [a] "r"(a), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "xer"); \
		*res = r; \
		*flg = Flags(x, c); \
	}

#define OPSI(X) \
	X(addic_m1, "addic", -1) X(addic_1, "addic", 1) X(addic_32767, "addic", 32767) \
	X(addic__m32768, "addic.", -32768) X(addic__1, "addic.", 1) \
	X(subfic_0, "subfic", 0) X(subfic_m1, "subfic", -1) X(subfic_100, "subfic", 100) \
	X(mulli_m3, "mulli", -3) X(mulli_32767, "mulli", 32767) X(mulli_m32768, "mulli", -32768) \
	X(srawi_0, "srawi", 0) X(srawi_1, "srawi", 1) X(srawi_4, "srawi", 4) X(srawi_31, "srawi", 31) \
	X(srawi__7, "srawi.", 7) \
	X(andi__0xff, "andi.", 0xff) X(andi__0x8000, "andi.", 0x8000) X(andis__0x8000, "andis.", 0x8000) \
	X(ori_0x1234, "ori", 0x1234) X(oris_0xffff, "oris", 0xffff) \
	X(xori_0xffff, "xori", 0xffff) X(xoris_0x8000, "xoris", 0x8000)

// addic and friends don't treat rA=0 as literal zero, so "r" is fine for all of these
OPSI(GEN_OPI)

static void RunOpI(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	static const u32 xins[] = {0, XER_SO | XER_CA};
	for (u32 x = 0; x < 2; x++)
	{
		for (u32 i = 0; i < NUM_VALS_EXT; i++)
		{
			u32 r, f;
			fn(kValsExt[i], 0, xins[x], &r, &f);
			Put(r);
			Put(f);
		}
	}
}

/* --- rotates --- */

#define GEN_RLWINM(id, mn, sh, mb, me) \
	static NOINLINE void rlwinm_##id(u32 a, u32 b, u32 xin, u32* res, u32* flg) \
	{ \
		u32 r, c; \
		(void)b; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0x80,%[z]\n\t" \
			mn " %[r],%[a]," #sh "," #mb "," #me "\n\t" \
			"mfcr %[c]" \
			: [r] "=&r"(r), [c] "=&r"(c) \
			: [a] "r"(a), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "xer"); \
		*res = r; \
		*flg = c >> 28; \
	}

#define RLWINM_OPS(X) \
	X(a, "rlwinm", 0, 0, 31) X(b, "rlwinm", 4, 0, 27) X(c, "rlwinm", 28, 4, 31) \
	X(d, "rlwinm", 8, 24, 31) X(e, "rlwinm", 31, 0, 0) X(f, "rlwinm", 16, 16, 15) \
	X(g, "rlwinm", 5, 28, 3) X(h, "rlwinm", 0, 31, 31) X(i, "rlwinm", 1, 1, 30) \
	X(j, "rlwinm.", 0, 16, 31) X(k, "rlwinm.", 3, 0, 0) X(l, "rlwinm.", 24, 8, 7)

RLWINM_OPS(GEN_RLWINM)

static void RunRot1(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	for (u32 i = 0; i < NUM_VALS_EXT; i++)
	{
		u32 r, f;
		fn(kValsExt[i], 0, XER_SO, &r, &f);
		Put(r);
		Put(f);
	}
}

#define GEN_RLWIMI(id, mn, sh, mb, me) \
	static NOINLINE void rlwimi_##id(u32 a, u32 b, u32 xin, u32* res, u32* flg) \
	{ \
		u32 r = b, c; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0x80,%[z]\n\t" \
			mn " %[r],%[a]," #sh "," #mb "," #me "\n\t" \
			"mfcr %[c]" \
			: [r] "+&r"(r), [c] "=&r"(c) \
			: [a] "r"(a), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "xer"); \
		*res = r; \
		*flg = c >> 28; \
	}

#define RLWIMI_OPS(X) \
	X(a, "rlwimi", 0, 0, 31) X(b, "rlwimi", 8, 8, 15) X(c, "rlwimi", 16, 0, 15) \
	X(d, "rlwimi", 4, 28, 3) X(e, "rlwimi", 31, 0, 0) X(f, "rlwimi", 24, 16, 7) \
	X(g, "rlwimi.", 0, 24, 31) X(h, "rlwimi.", 12, 4, 19)

RLWIMI_OPS(GEN_RLWIMI)

static void RunRot2(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	for (u32 i = 0; i < NUM_VALS; i++)
	{
		for (u32 j = 0; j < NUM_VALS; j++)
		{
			u32 r, f;
			fn(kVals[i], kVals[j], XER_SO, &r, &f);
			Put(r);
			Put(f);
		}
	}
}

#define GEN_RLWNM(id, mn, mb, me) \
	static NOINLINE void rlwnm_##id(u32 a, u32 b, u32 xin, u32* res, u32* flg) \
	{ \
		u32 r, c; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0x80,%[z]\n\t" \
			mn " %[r],%[a],%[b]," #mb "," #me "\n\t" \
			"mfcr %[c]" \
			: [r] "=&r"(r), [c] "=&r"(c) \
			: [a] "r"(a), [b] "r"(b), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "xer"); \
		*res = r; \
		*flg = c >> 28; \
	}

#define RLWNM_OPS(X) \
	X(a, "rlwnm", 0, 31) X(b, "rlwnm", 16, 31) X(c, "rlwnm", 28, 3) X(d, "rlwnm.", 0, 7)

RLWNM_OPS(GEN_RLWNM)

/* --- compares --- */

#define GEN_CMP(id, insn, field) \
	static NOINLINE u32 cmp_##id(u32 a, u32 b, u32 xin) \
	{ \
		u32 c; \
		(void)b; \
		__asm__ volatile( \
			"mtxer %[xi]\n\t" \
			"mtcrf 0xff,%[z]\n\t" \
			insn "\n\t" \
			"mfcr %[c]" \
			: [c] "=&r"(c) \
			: [a] "r"(a), [b] "r"(b), [xi] "r"(xin), [z] "r"(0) \
			: "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7", "xer"); \
		return (c >> (28 - (field) * 4)) & 0xF; \
	}

typedef u32 (*CmpFn)(u32 a, u32 b, u32 xin);

#define CMP_OPS(X) \
	X(cmpw, "cmpw %[a],%[b]", 0) X(cmpw_cr7, "cmpw cr7,%[a],%[b]", 7) \
	X(cmplw, "cmplw %[a],%[b]", 0) X(cmplw_cr3, "cmplw cr3,%[a],%[b]", 3) \
	X(cmpwi_0, "cmpwi %[a],0", 0) X(cmpwi_m1, "cmpwi cr1,%[a],-1", 1) \
	X(cmpwi_32767, "cmpwi cr6,%[a],32767", 6) \
	X(cmplwi_0, "cmplwi %[a],0", 0) X(cmplwi_65535, "cmplwi cr5,%[a],65535", 5) \
	X(cmplwi_1, "cmplwi cr2,%[a],1", 2)

CMP_OPS(GEN_CMP)

// packs the 4 bit CR field results, 8 per word
static void RunCmp(const TestDef* t)
{
	CmpFn fn = (CmpFn)t->ctx;
	static const u32 xins[] = {0, XER_SO};
	for (u32 x = 0; x < 2; x++)
	{
		u32 packed = 0, n = 0;
		for (u32 i = 0; i < NUM_VALS_EXT; i++)
		{
			for (u32 j = 0; j < NUM_VALS_EXT; j++)
			{
				packed = (packed << 4) | fn(kValsExt[i], kValsExt[j], xins[x]);
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
	}
}

/* --- CR logical ops, mcrf --- */

typedef u32 (*CrFn)(u32 cr);

#define GEN_CR(id, insn) \
	static NOINLINE u32 cr_##id(u32 cr) \
	{ \
		u32 c; \
		__asm__ volatile( \
			"mtcrf 0xff,%[in]\n\t" \
			insn "\n\t" \
			"mfcr %[c]" \
			: [c] "=&r"(c) \
			: [in] "r"(cr) \
			: "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7"); \
		return c; \
	}

#define CR_OPS(X) \
	X(crand_a, "crand 0,1,2") X(crand_b, "crand 31,4,30") \
	X(cror_a, "cror 2,29,13") X(cror_b, "cror 9,9,9") \
	X(crxor_a, "crxor 6,6,6") X(crxor_b, "crxor 17,3,22") \
	X(crnand_a, "crnand 17,3,22") X(crnand_b, "crnand 0,0,0") \
	X(crnor_a, "crnor 8,9,10") X(crnor_b, "crnor 31,31,31") \
	X(creqv_a, "creqv 24,24,24") X(creqv_b, "creqv 1,12,23") \
	X(crandc_a, "crandc 12,0,31") X(crandc_b, "crandc 5,5,7") \
	X(crorc_a, "crorc 5,27,14") X(crorc_b, "crorc 30,30,30") \
	X(mcrf_a, "mcrf 7,0") X(mcrf_b, "mcrf 0,5") X(mcrf_c, "mcrf 3,3")

CR_OPS(GEN_CR)

static const u32 kCrPatterns[] = {0x00000000, 0xFFFFFFFF, 0x5A3C96F0, 0x12345678, 0x80000001, 0x0F0F0F0F};
#define NUM_CR_PATTERNS (sizeof(kCrPatterns) / sizeof(kCrPatterns[0]))

static void RunCr(const TestDef* t)
{
	CrFn fn = (CrFn)t->ctx;
	for (u32 i = 0; i < NUM_CR_PATTERNS; i++)
		Put(fn(kCrPatterns[i]));
}

/* --- mtcrf / mfcr --- */

#define GEN_MTCRF(id, mask) \
	static NOINLINE u32 mtcrf_##id(u32 cr) \
	{ \
		u32 c; \
		__asm__ volatile( \
			"mtcrf 0xff,%[base]\n\t" \
			"mtcrf " #mask ",%[in]\n\t" \
			"mfcr %[c]" \
			: [c] "=&r"(c) \
			: [in] "r"(cr), [base] "r"(0xA5C3E187) \
			: "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7"); \
		return c; \
	}

#define MTCRF_OPS(X) \
	X(00, 0x00) X(01, 0x01) X(80, 0x80) X(3c, 0x3c) X(5a, 0x5a) X(ff, 0xff)

MTCRF_OPS(GEN_MTCRF)

/* --- XER moves: mtxer/mfxer, mcrxr --- */

static NOINLINE u32 XerRoundtrip(u32 v)
{
	u32 r;
	__asm__ volatile(
		"mtxer %[v]\n\t"
		"mfxer %[r]"
		: [r] "=&r"(r)
		: [v] "r"(v)
		: "xer");
	return r;
}

static void RunXerMoves(const TestDef* t)
{
	static const u32 vals[] = {0x00000000, 0xFFFFFFFF, 0x80000000, 0x40000000, 0x20000000, 0xE000007F, 0x00000012, 0x1FFFFF80};
	(void)t;
	for (u32 i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
		Put(XerRoundtrip(vals[i]));
}

static NOINLINE void Mcrxr(u32 xin, u32* crOut, u32* xerOut)
{
	u32 c, x;
	__asm__ volatile(
		"mtxer %[xi]\n\t"
		"mtcrf 0xff,%[base]\n\t"
		"mcrxr cr3\n\t"
		"mfcr %[c]\n\t"
		"mfxer %[x]"
		: [c] "=&r"(c), [x] "=&r"(x)
		: [xi] "r"(xin), [base] "r"(0x5A5A5A5A)
		: "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7", "xer");
	*crOut = c;
	*xerOut = x;
}

static void RunMcrxr(const TestDef* t)
{
	static const u32 vals[] = {0x00000000, 0xE0000000, 0x80000000, 0x40000000, 0x20000000, 0xA0000005};
	(void)t;
	for (u32 i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
	{
		u32 c, x;
		Mcrxr(vals[i], &c, &x);
		Put(c);
		Put(x & 0xE000007F);
	}
}

/* --- CR0 effects of record forms on a long dependency chain --- */

// mixes carries through a chain so a wrong CA/SO in any step changes the result
static NOINLINE u32 CarryChain(u32 a, u32 b, u32 n)
{
	u32 x = a, y = b;
	for (u32 i = 0; i < n; i++)
	{
		u32 t0, t1;
		__asm__ volatile(
			"addc %[t0],%[x],%[y]\n\t"
			"adde %[t1],%[y],%[t0]\n\t"
			"subfe %[x],%[t1],%[x]\n\t"
			"addze %[y],%[t1]\n\t"
			"subfze %[t0],%[x]\n\t"
			"addme %[t1],%[t0]\n\t"
			"rotlwi %[y],%[y],7\n\t"
			"xor %[x],%[x],%[t1]"
			: [x] "+&r"(x), [y] "+&r"(y), [t0] "=&r"(t0), [t1] "=&r"(t1)
			:
			: "xer");
	}
	return x ^ (y * 31);
}

static void RunCarryChain(const TestDef* t)
{
	(void)t;
	Put(CarryChain(0x12345678, 0x9ABCDEF0, 1000));
	Put(CarryChain(0xFFFFFFFF, 0x00000001, 1000));
	Put(CarryChain(0x80000000, 0x80000000, 777));
}

/* --- pseudo random fuzzing, one hash per op --- */

#define FUZZ_ITERATIONS 512

static void RunFuzz2(const TestDef* t)
{
	IntOp2Fn fn = (IntOp2Fn)t->ctx;
	u32 seed = 0xC0FFEE11;
	u32 h = FNV_INIT;
	for (u32 i = 0; i < FUZZ_ITERATIONS; i++)
	{
		u32 a = Lcg(&seed);
		u32 b = Lcg(&seed);
		u32 sel = Lcg(&seed);
		// bias towards small and boundary values
		if (sel & 0x10)
			a >>= (sel & 31);
		if (sel & 0x20)
			b >>= ((sel >> 8) & 31);
		if ((sel & 0x1C0) == 0x40)
			b = 0;
		if ((sel & 0x1C0) == 0x80)
			b = 0xFFFFFFFF;
		u32 xin = sel & 0xA0000000;
		u32 r, f;
		fn(a, b, xin, &r, &f);
		h = Fnv1a(h, r);
		h = Fnv1a(h, f);
	}
	Put(h);
}

/* --- test table --- */

#define TAB_ARITH2(id, mn) {mn, RunArith2, (const void*)op2_##id},
#define TAB_LOGIC2(id, mn) {mn, RunLogic2, (const void*)op2_##id},
#define TAB_SHIFT2(id, mn) {mn, RunShift2, (const void*)op2_##id},
#define TAB_OP1(id, mn) {mn, RunOp1, (const void*)op1_##id},
#define TAB_OPI(id, mn, imm) {mn "," #imm, RunOpI, (const void*)opi_##id},
#define TAB_RLWINM(id, mn, sh, mb, me) {mn "," #sh "," #mb "," #me, RunRot1, (const void*)rlwinm_##id},
#define TAB_RLWIMI(id, mn, sh, mb, me) {mn "," #sh "," #mb "," #me, RunRot2, (const void*)rlwimi_##id},
#define TAB_RLWNM(id, mn, mb, me) {mn "," #mb "," #me, RunShift2, (const void*)rlwnm_##id},
#define TAB_CMP(id, insn, field) {#id, RunCmp, (const void*)cmp_##id},
#define TAB_CR(id, insn) {insn, RunCr, (const void*)cr_##id},
#define TAB_MTCRF(id, mask) {"mtcrf," #mask, RunCr, (const void*)mtcrf_##id},
#define TAB_FUZZ2(id, mn) {"fuzz." mn, RunFuzz2, (const void*)op2_##id},
#define TAB_FUZZ1(id, mn) {"fuzz." mn, RunFuzz2, (const void*)op1_##id},

const TestDef g_intTests[] = {
	ARITH_OPS2(TAB_ARITH2)
	OPS1(TAB_OP1)
	OPSI(TAB_OPI)
	LOGIC_OPS2(TAB_LOGIC2)
	SHIFT_OPS2(TAB_SHIFT2)
	RLWINM_OPS(TAB_RLWINM)
	RLWIMI_OPS(TAB_RLWIMI)
	RLWNM_OPS(TAB_RLWNM)
	CMP_OPS(TAB_CMP)
	CR_OPS(TAB_CR)
	MTCRF_OPS(TAB_MTCRF)
	{"mtxer_mfxer", RunXerMoves, 0},
	{"mcrxr", RunMcrxr, 0},
	{"carry_chain", RunCarryChain, 0},
	ARITH_OPS2(TAB_FUZZ2)
	SHIFT_OPS2(TAB_FUZZ2)
	OPS1(TAB_FUZZ1)
};

const u32 g_intTestCount = sizeof(g_intTests) / sizeof(g_intTests[0]);
