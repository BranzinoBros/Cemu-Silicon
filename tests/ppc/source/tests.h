// Test registry shared by the ppc_tests source files.
#pragma once

#include "common.h"

typedef struct TestDef TestDef;
typedef void (*TestRunFn)(const TestDef* t);

struct TestDef
{
	const char* name; // printed as "name: <hex words>"
	TestRunFn run;
	const void* ctx; // test specific (usually a wrapper function pointer)
};

// append result words for the currently running test
void Put(u32 v);
void Put64(u64 v);
// append the raw bits of a double (as stored by stfd)
void PutD(double d);

static inline u64 DoubleBits(double d)
{
	u64 v;
	memcpy(&v, &d, sizeof(v));
	return v;
}

static inline double BitsDouble(u64 v)
{
	double d;
	memcpy(&d, &v, sizeof(d));
	return d;
}

static inline float BitsFloat(u32 v)
{
	float f;
	memcpy(&f, &v, sizeof(f));
	return f;
}

extern const TestDef g_intTests[];
extern const u32 g_intTestCount;
extern const TestDef g_memTests[];
extern const u32 g_memTestCount;
extern const TestDef g_fpuTests[];
extern const u32 g_fpuTestCount;
extern const TestDef g_psTests[];
extern const u32 g_psTestCount;
