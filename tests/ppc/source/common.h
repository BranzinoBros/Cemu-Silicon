// Shared helpers for the Cemu-Silicon PowerPC test and benchmark programs.
//
// Output goes through coreinit's OSConsoleWrite, which Cemu forwards to stdout
// when started with --forward-console-logging and writes to log.txt when
// "Coreinit Logging (OSReport/OSConsole)" is enabled. Formatting is done here
// instead of via OSReport/printf so the emulator's printf implementation can't
// influence the output.
#pragma once

#include <stdint.h>
#include <string.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define NOINLINE __attribute__((noinline))

// log.txt splits console lines at 270 characters, so keep lines well below that
#define OUT_LINE_MAX 256

typedef struct
{
	char buf[OUT_LINE_MAX + 2];
	u32 len;
} OutLine;

static inline void OutReset(OutLine* l)
{
	l->len = 0;
}

static inline void OutStr(OutLine* l, const char* s)
{
	while (*s && l->len < OUT_LINE_MAX)
		l->buf[l->len++] = *s++;
}

static inline void OutHex32(OutLine* l, u32 v)
{
	static const char digits[] = "0123456789abcdef";
	for (int i = 7; i >= 0; i--)
	{
		if (l->len < OUT_LINE_MAX)
			l->buf[l->len++] = digits[(v >> (i * 4)) & 0xF];
	}
}

static inline void OutDec(OutLine* l, u64 v)
{
	char tmp[24];
	int n = 0;
	do
	{
		tmp[n++] = (char)('0' + (v % 10));
		v /= 10;
	} while (v);
	while (n && l->len < OUT_LINE_MAX)
		l->buf[l->len++] = tmp[--n];
}

static inline void OutFlush(OutLine* l)
{
	l->buf[l->len++] = '\n';
	OSConsoleWrite(l->buf, l->len);
	l->len = 0;
}

static inline void PrintLine(const char* s)
{
	OutLine l;
	OutReset(&l);
	OutStr(&l, s);
	OutFlush(&l);
}

static inline u32 Fnv1a(u32 h, u32 v)
{
	for (int i = 0; i < 4; i++)
	{
		h ^= (v >> 24) & 0xFF;
		h *= 16777619u;
		v <<= 8;
	}
	return h;
}

#define FNV_INIT 2166136261u

static inline u32 Lcg(u32* state)
{
	*state = *state * 1664525u + 1013904223u;
	return *state;
}

static inline void SleepMs(u32 ms)
{
	OSSleepTicks(OSMillisecondsToTicks(ms));
}
