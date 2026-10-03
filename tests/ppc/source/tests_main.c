// ppc_tests: deterministic Espresso instruction tests for Cemu.
//
// Every test runs NUM_PASSES times. Cemu's recompiler compiles functions
// asynchronously after they were first executed, so the first pass mostly runs
// in the interpreter while later passes run recompiled code. Only the results
// of the final pass are printed. If any pass produced different words, a
// "# unstable: <name>" diagnostic is printed as well, which already hints at a
// recompiler/interpreter mismatch within a single run.
//
// Output format (one line per test, long results are split into name.N lines):
//   PPC_TESTS_BEGIN
//   <name>: <hex word> <hex word> ...
//   checksum: <fnv1a over all result words>
//   PPC_TESTS_DONE

#include "tests.h"

#define NUM_PASSES 6
#define PASS_SLEEP_MS 40
#define MAX_WORDS_PER_TEST 4096
#define WORDS_PER_LINE 24

static u32 s_words[MAX_WORDS_PER_TEST];
static u32 s_wordCount;
static u32 s_overflow;

void Put(u32 v)
{
	if (s_wordCount < MAX_WORDS_PER_TEST)
		s_words[s_wordCount++] = v;
	else
		s_overflow = 1;
}

void Put64(u64 v)
{
	Put((u32)(v >> 32));
	Put((u32)v);
}

void PutD(double d)
{
	Put64(DoubleBits(d));
}

typedef struct
{
	const TestDef* def;
	u32 hash;  // hash of the final pass words
	u32 count; // number of words of the final pass
	u32 firstHash;
	u32 firstCount;
	u32 unstable;
} TestState;

#define MAX_TESTS 1024
static TestState s_tests[MAX_TESTS];
static u32 s_testCount;

static void AddTests(const TestDef* defs, u32 count)
{
	for (u32 i = 0; i < count && s_testCount < MAX_TESTS; i++)
	{
		s_tests[s_testCount].def = defs + i;
		s_testCount++;
	}
}

static u32 HashWords(void)
{
	u32 h = FNV_INIT;
	for (u32 i = 0; i < s_wordCount; i++)
		h = Fnv1a(h, s_words[i]);
	return h;
}

static void RunTest(TestState* ts)
{
	s_wordCount = 0;
	s_overflow = 0;
	ts->def->run(ts->def);
}

static void PrintTest(TestState* ts, u32* checksum)
{
	OutLine l;
	u32 numLines = (s_wordCount + WORDS_PER_LINE - 1) / WORDS_PER_LINE;
	if (numLines == 0)
		numLines = 1;
	for (u32 line = 0; line < numLines; line++)
	{
		OutReset(&l);
		OutStr(&l, ts->def->name);
		if (numLines > 1)
		{
			OutStr(&l, ".");
			OutDec(&l, line);
		}
		OutStr(&l, ":");
		u32 end = (line + 1) * WORDS_PER_LINE;
		if (end > s_wordCount)
			end = s_wordCount;
		for (u32 i = line * WORDS_PER_LINE; i < end; i++)
		{
			OutStr(&l, " ");
			OutHex32(&l, s_words[i]);
			*checksum = Fnv1a(*checksum, s_words[i]);
		}
		OutFlush(&l);
	}
	if (s_overflow)
	{
		OutReset(&l);
		OutStr(&l, "# overflow: ");
		OutStr(&l, ts->def->name);
		OutFlush(&l);
	}
}

int main(int argc, char** argv)
{
	AddTests(g_intTests, g_intTestCount);
	AddTests(g_memTests, g_memTestCount);
	AddTests(g_fpuTests, g_fpuTestCount);
	AddTests(g_psTests, g_psTestCount);

	PrintLine("PPC_TESTS_BEGIN");

	// warm-up passes, gives the recompiler time to translate every test
	for (u32 pass = 0; pass < NUM_PASSES - 1; pass++)
	{
		for (u32 i = 0; i < s_testCount; i++)
		{
			TestState* ts = s_tests + i;
			RunTest(ts);
			u32 h = HashWords();
			if (pass == 0)
			{
				ts->firstHash = h;
				ts->firstCount = s_wordCount;
			}
			else if (h != ts->firstHash || s_wordCount != ts->firstCount)
				ts->unstable = 1;
		}
		SleepMs(PASS_SLEEP_MS);
	}

	// final pass, print results
	u32 checksum = FNV_INIT;
	u32 unstableCount = 0;
	for (u32 i = 0; i < s_testCount; i++)
	{
		TestState* ts = s_tests + i;
		RunTest(ts);
		ts->hash = HashWords();
		ts->count = s_wordCount;
		if (ts->hash != ts->firstHash || ts->count != ts->firstCount)
			ts->unstable = 1;
		PrintTest(ts, &checksum);
	}

	OutLine l;
	for (u32 i = 0; i < s_testCount; i++)
	{
		if (!s_tests[i].unstable)
			continue;
		unstableCount++;
		OutReset(&l);
		OutStr(&l, "# unstable: ");
		OutStr(&l, s_tests[i].def->name);
		OutFlush(&l);
	}

	OutReset(&l);
	OutStr(&l, "# tests: ");
	OutDec(&l, s_testCount);
	OutStr(&l, " unstable: ");
	OutDec(&l, unstableCount);
	OutFlush(&l);

	OutReset(&l);
	OutStr(&l, "checksum: ");
	OutHex32(&l, checksum);
	OutFlush(&l);

	PrintLine("PPC_TESTS_DONE");
	return 0;
}
