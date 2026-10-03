#!/usr/bin/env bash
# Runs the homebrew PowerPC tests (tests/ppc) in Cemu, once with the recompiler
# and once with --force-interpreter, and compares the results. The interpreter
# is used as the reference: a test whose output differs is reported as a likely
# recompiler bug. Also runs the CPU benchmarks and prints a timing table.
#
# Cemu runs from a temporary copy in portable mode, so the user's real Cemu
# settings (~/Library/Application Support/Cemu) are never touched.
#
# Usage: scripts/run-ppc-tests.sh [options] [path to Cemu executable or .app]
# See --help for options. Written for the bash 3.2 that ships with macOS.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CEMU_PATH="$REPO_DIR/bin/Cemu_release.app/Contents/MacOS/Cemu_release"
RPX_DIR=""
RUN_TESTS=1
RUN_BENCH=1
BENCH_INTERPRETER=1
TIMEOUT=900
KEEP=0
OUT_DIR=""

usage()
{
	cat <<EOF
Usage: $(basename "$0") [options] [CEMU]

CEMU is the Cemu executable or .app bundle
(default: bin/Cemu_release.app/Contents/MacOS/Cemu_release).

Options:
  --rpx-dir DIR          directory with ppc_tests.rpx/ppc_bench.rpx
                         (default: tests/ppc/build, else tests/ppc/prebuilt)
  --tests-only           only run ppc_tests
  --bench-only           only run ppc_bench
  --no-interpreter-bench skip the (slow) interpreter run of ppc_bench
  --timeout SEC          per run timeout in seconds (default: $TIMEOUT)
  --out DIR              directory for logs and results (default: a temp dir)
  --keep                 keep the temporary Cemu copy and logs
  -h, --help             show this help

Exit status: 0 = all tests match, 1 = recompiler/interpreter mismatch,
2 = a run failed (timeout, crash, missing marker).
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--rpx-dir) RPX_DIR="$2"; shift 2 ;;
		--tests-only) RUN_BENCH=0; shift ;;
		--bench-only) RUN_TESTS=0; shift ;;
		--no-interpreter-bench) BENCH_INTERPRETER=0; shift ;;
		--timeout) TIMEOUT="$2"; shift 2 ;;
		--out) OUT_DIR="$2"; shift 2 ;;
		--keep) KEEP=1; shift ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) CEMU_PATH="$1"; shift ;;
	esac
done

die()
{
	echo "error: $*" >&2
	exit 2
}

now()
{
	perl -MTime::HiRes=time -e 'printf "%.3f\n", time'
}

# --- locate inputs ---

if [ -z "$RPX_DIR" ]; then
	if [ -f "$REPO_DIR/tests/ppc/build/ppc_tests.rpx" ]; then
		RPX_DIR="$REPO_DIR/tests/ppc/build"
	else
		RPX_DIR="$REPO_DIR/tests/ppc/prebuilt"
	fi
fi
RPX_DIR="$(cd "$RPX_DIR" 2>/dev/null && pwd)" || die "rpx directory not found"
[ "$RUN_TESTS" = 0 ] || [ -f "$RPX_DIR/ppc_tests.rpx" ] || die "$RPX_DIR/ppc_tests.rpx not found (build it with tests/ppc/build.sh)"
[ "$RUN_BENCH" = 0 ] || [ -f "$RPX_DIR/ppc_bench.rpx" ] || die "$RPX_DIR/ppc_bench.rpx not found (build it with tests/ppc/build.sh)"

[ -e "$CEMU_PATH" ] || die "Cemu not found at $CEMU_PATH"
CEMU_PATH="$(cd "$(dirname "$CEMU_PATH")" && pwd)/$(basename "$CEMU_PATH")"

# accept either the .app bundle or the executable inside it
APP_BUNDLE=""
case "$CEMU_PATH" in
	*.app) APP_BUNDLE="$CEMU_PATH" ;;
	*.app/Contents/MacOS/*) APP_BUNDLE="${CEMU_PATH%/Contents/MacOS/*}" ;;
esac

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cemu-ppc-tests.XXXXXX")" || die "mktemp failed"
[ -n "$OUT_DIR" ] || OUT_DIR="$WORK_DIR/results"
mkdir -p "$OUT_DIR" || die "cannot create $OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

CEMU_PID=""
cleanup()
{
	if [ -n "$CEMU_PID" ] && kill -0 "$CEMU_PID" 2>/dev/null; then
		kill "$CEMU_PID" 2>/dev/null
		sleep 1
		kill -9 "$CEMU_PID" 2>/dev/null
	fi
	if [ "$KEEP" = 0 ]; then
		rm -rf "$WORK_DIR/run"
		case "$OUT_DIR" in
			"$WORK_DIR"/*) ;; # results are inside WORK_DIR, keep them
			*) rmdir "$WORK_DIR" 2>/dev/null ;;
		esac
	fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# clone (APFS copy-on-write when possible) so the copy costs no disk space
copy_tree()
{
	cp -cR "$1" "$2" 2>/dev/null || cp -R "$1" "$2"
}

# --- create an isolated portable Cemu ---
# Cemu uses a "portable" directory next to the .app bundle (or next to the
# executable when it isn't in a bundle) for settings, logs and caches.

RUN_DIR="$WORK_DIR/run"
mkdir -p "$RUN_DIR"
if [ -n "$APP_BUNDLE" ]; then
	APP_NAME="$(basename "$APP_BUNDLE")"
	EXE_NAME="$(basename "$CEMU_PATH")"
	[ "$CEMU_PATH" = "$APP_BUNDLE" ] && EXE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP_BUNDLE/Contents/Info.plist" 2>/dev/null)"
	[ -n "$EXE_NAME" ] || die "cannot determine the executable inside $APP_BUNDLE"
	copy_tree "$APP_BUNDLE" "$RUN_DIR/$APP_NAME" || die "copying $APP_BUNDLE failed"
	xattr -dr com.apple.quarantine "$RUN_DIR/$APP_NAME" 2>/dev/null
	CEMU_EXE="$RUN_DIR/$APP_NAME/Contents/MacOS/$EXE_NAME"
	PORTABLE_DIR="$RUN_DIR/portable"
else
	# plain executable: copy it and link the data directories next to it
	mkdir -p "$RUN_DIR/bin"
	copy_tree "$CEMU_PATH" "$RUN_DIR/bin/" || die "copying $CEMU_PATH failed"
	for d in resources gameProfiles; do
		[ -e "$(dirname "$CEMU_PATH")/$d" ] && ln -s "$(dirname "$CEMU_PATH")/$d" "$RUN_DIR/bin/$d"
	done
	CEMU_EXE="$RUN_DIR/bin/$(basename "$CEMU_PATH")"
	PORTABLE_DIR="$RUN_DIR/bin/portable"
fi
[ -x "$CEMU_EXE" ] || die "$CEMU_EXE is not executable"

prepare_portable()
{
	rm -rf "$PORTABLE_DIR"
	mkdir -p "$PORTABLE_DIR/mlc01"
	# an existing settings.xml skips the Getting Started dialog. logflag 131072
	# (bit 17) enables "Coreinit Logging" so OSConsole output also reaches log.txt
	cat > "$PORTABLE_DIR/settings.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<content>
    <logflag>131072</logflag>
    <check_update>false</check_update>
    <use_discord_presence>false</use_discord_presence>
    <play_boot_sound>false</play_boot_sound>
</content>
EOF
}

# --- run one rpx ---
# run_cemu <rpx> <marker> <tag> [extra Cemu args...]
# Writes $OUT_DIR/<tag>.stdout/.stderr/.log.txt/.results and sets RUN_WALL.
RUN_WALL=""
run_cemu()
{
	local rpx="$1" marker="$2" tag="$3"
	shift 3
	local stdout="$OUT_DIR/$tag.stdout" stderr="$OUT_DIR/$tag.stderr"
	prepare_portable
	echo "  running $(basename "$rpx") [$tag] ..." >&2
	local start end status=0
	start="$(now)"
	"$CEMU_EXE" -g "$rpx" -m "$PORTABLE_DIR/mlc01" --forward-console-logging ${1+"$@"} > "$stdout" 2> "$stderr" &
	CEMU_PID=$!
	local deadline=$(( $(date +%s) + TIMEOUT ))
	while :; do
		if grep -q "^$marker" "$stdout" 2>/dev/null; then
			break
		fi
		if ! kill -0 "$CEMU_PID" 2>/dev/null; then
			# Cemu exited; stdout may still have been flushed with the marker
			grep -q "^$marker" "$stdout" 2>/dev/null || status=1
			break
		fi
		if [ "$(date +%s)" -ge "$deadline" ]; then
			status=2
			break
		fi
		sleep 0.2
	done
	end="$(now)"
	RUN_WALL="$(awk -v a="$start" -v b="$end" 'BEGIN { printf "%.2f", b - a }')"
	if kill -0 "$CEMU_PID" 2>/dev/null; then
		kill "$CEMU_PID" 2>/dev/null
		local i=0
		while kill -0 "$CEMU_PID" 2>/dev/null && [ $i -lt 25 ]; do
			sleep 0.2
			i=$((i + 1))
		done
		kill -9 "$CEMU_PID" 2>/dev/null
	fi
	wait "$CEMU_PID" 2>/dev/null
	CEMU_PID=""
	cp "$PORTABLE_DIR/log.txt" "$OUT_DIR/$tag.log.txt" 2>/dev/null

	# prefer stdout; fall back to the OSConsole lines in log.txt
	local src="$stdout"
	if ! grep -q "^$marker" "$stdout" 2>/dev/null && grep -q "\[OSConsole\] $marker" "$OUT_DIR/$tag.log.txt" 2>/dev/null; then
		sed -n 's/^.*\[OSConsole\] //p' "$OUT_DIR/$tag.log.txt" > "$OUT_DIR/$tag.console"
		src="$OUT_DIR/$tag.console"
		status=0
	fi
	cp "$src" "$OUT_DIR/$tag.output"
	case $status in
		1) echo "  [$tag] Cemu exited before printing $marker (see $stderr and $OUT_DIR/$tag.log.txt)" >&2 ;;
		2) echo "  [$tag] timed out after ${TIMEOUT}s without $marker" >&2 ;;
	esac
	return $status
}

# result lines between the BEGIN marker and the DONE marker, no diagnostics
extract_results()
{
	awk -v beginMarker="$2" -v endMarker="$3" '
		$0 ~ "^" beginMarker { on = 1; next }
		$0 ~ "^" endMarker { exit }
		on && $0 !~ /^#/ && index($0, ": ") > 0 { print }
	' "$1"
}

FAILED=0
MISMATCH=0

# --- correctness tests ---

if [ "$RUN_TESTS" = 1 ]; then
	echo "== ppc_tests =="
	run_cemu "$RPX_DIR/ppc_tests.rpx" PPC_TESTS_DONE tests-recompiler || FAILED=1
	JIT_WALL="$RUN_WALL"
	run_cemu "$RPX_DIR/ppc_tests.rpx" PPC_TESTS_DONE tests-interpreter --force-interpreter || FAILED=1
	INT_WALL="$RUN_WALL"

	extract_results "$OUT_DIR/tests-recompiler.output" PPC_TESTS_BEGIN PPC_TESTS_DONE > "$OUT_DIR/tests-recompiler.results"
	extract_results "$OUT_DIR/tests-interpreter.output" PPC_TESTS_BEGIN PPC_TESTS_DONE > "$OUT_DIR/tests-interpreter.results"
	JIT_LINES=$(wc -l < "$OUT_DIR/tests-recompiler.results" | tr -d ' ')
	INT_LINES=$(wc -l < "$OUT_DIR/tests-interpreter.results" | tr -d ' ')
	JIT_SUM=$(sed -n 's/^checksum: //p' "$OUT_DIR/tests-recompiler.results")
	INT_SUM=$(sed -n 's/^checksum: //p' "$OUT_DIR/tests-interpreter.results")
	printf "  recompiler : %5s lines  checksum %-8s  wall %ss\n" "$JIT_LINES" "${JIT_SUM:-none}" "$JIT_WALL"
	printf "  interpreter: %5s lines  checksum %-8s  wall %ss\n" "$INT_LINES" "${INT_SUM:-none}" "$INT_WALL"

	if [ "$JIT_LINES" -eq 0 ] || [ "$INT_LINES" -eq 0 ]; then
		echo "  no results to compare"
		FAILED=1
	else
		# compare line by line keyed by test name. Results of long tests are
		# split into name.0, name.1, ...; report the test name once
		awk -F': ' '
			NR == FNR { ref[$1] = $2; order[++n] = $1; next }
			{ jit[$1] = $2; if (!($1 in ref)) order[++n] = $1 }
			END {
				for (i = 1; i <= n; i++) {
					k = order[i]
					if (k == "checksum" || ref[k] == jit[k]) continue
					base = k; sub(/\.[0-9]+$/, "", base)
					if (!(base in seen)) { seen[base] = 1; names[++m] = base }
					detail[base] = detail[base] sprintf("    %s\n      interpreter: %s\n      recompiler : %s\n", k, (k in ref) ? ref[k] : "<missing>", (k in jit) ? jit[k] : "<missing>")
				}
				if (m == 0) exit 0
				printf "  MISMATCH: %d test(s) differ, likely recompiler bugs (interpreter is the reference):\n", m
				for (i = 1; i <= m; i++) printf "  - %s\n", names[i]
				print ""
				for (i = 1; i <= m; i++) printf "%s", detail[names[i]]
				exit 1
			}
		' "$OUT_DIR/tests-interpreter.results" "$OUT_DIR/tests-recompiler.results" > "$OUT_DIR/tests-mismatch.txt"
		if [ $? -ne 0 ]; then
			MISMATCH=1
			# print the summary and the first details; the full list is in the file
			head -n 60 "$OUT_DIR/tests-mismatch.txt"
			[ "$(wc -l < "$OUT_DIR/tests-mismatch.txt")" -gt 60 ] && echo "  ... full report: $OUT_DIR/tests-mismatch.txt"
		else
			echo "  OK: recompiler and interpreter results are identical"
		fi
	fi
	# tests whose result changed between passes within one run (pass 1 mostly
	# runs interpreted, later passes recompiled)
	UNSTABLE=$(sed -n 's/^# unstable: //p' "$OUT_DIR/tests-recompiler.output" | tr '\n' ' ')
	[ -n "$UNSTABLE" ] && echo "  note: unstable across passes in the recompiler run: $UNSTABLE"
	UNSTABLE=$(sed -n 's/^# unstable: //p' "$OUT_DIR/tests-interpreter.output" | tr '\n' ' ')
	[ -n "$UNSTABLE" ] && echo "  note: unstable across passes in the interpreter run (test bug?): $UNSTABLE"
	echo
fi

# --- benchmarks ---

bench_value()
{
	# bench_value <output file> <name> <field>
	sed -n "s/^bench $2: .*$3=\([0-9a-f]*\).*/\1/p" "$1" | head -n 1
}

if [ "$RUN_BENCH" = 1 ]; then
	echo "== ppc_bench =="
	run_cemu "$RPX_DIR/ppc_bench.rpx" PPC_BENCH_DONE bench-recompiler || FAILED=1
	BJIT_WALL="$RUN_WALL"
	BINT_WALL="-"
	if [ "$BENCH_INTERPRETER" = 1 ]; then
		run_cemu "$RPX_DIR/ppc_bench.rpx" PPC_BENCH_DONE bench-interpreter --force-interpreter || FAILED=1
		BINT_WALL="$RUN_WALL"
	else
		: > "$OUT_DIR/bench-interpreter.output"
	fi
	BJ="$OUT_DIR/bench-recompiler.output"
	BI="$OUT_DIR/bench-interpreter.output"
	echo
	printf "  %-16s %14s %14s %9s  %s\n" "benchmark" "recompiler us" "interpreter us" "speedup" "checksum"
	for name in $(sed -n 's/^bench \([^:]*\):.*/\1/p' "$BJ"); do
		ju=$(bench_value "$BJ" "$name" us)
		jc=$(bench_value "$BJ" "$name" checksum)
		iu=$(bench_value "$BI" "$name" us)
		ic=$(bench_value "$BI" "$name" checksum)
		speedup="-"
		[ -n "$iu" ] && [ -n "$ju" ] && [ "$ju" -gt 0 ] && speedup=$(awk -v a="$iu" -v b="$ju" 'BEGIN { printf "%.1fx", a / b }')
		check="$jc"
		if [ -n "$ic" ] && [ "$ic" != "$jc" ]; then
			check="$jc MISMATCH (interpreter $ic)"
			MISMATCH=1
		fi
		printf "  %-16s %14s %14s %9s  %s\n" "$name" "$ju" "${iu:--}" "$speedup" "$check"
	done
	jt=$(sed -n 's/^bench_total: us=\([0-9]*\).*/\1/p' "$BJ")
	it=$(sed -n 's/^bench_total: us=\([0-9]*\).*/\1/p' "$BI")
	speedup="-"
	[ -n "$it" ] && [ -n "$jt" ] && [ "$jt" -gt 0 ] && speedup=$(awk -v a="$it" -v b="$jt" 'BEGIN { printf "%.1fx", a / b }')
	printf "  %-16s %14s %14s %9s\n" "total (guest)" "${jt:--}" "${it:--}" "$speedup"
	printf "  %-16s %13ss %13ss\n" "host wall time" "$BJIT_WALL" "$BINT_WALL"
	echo "  (host wall time includes Cemu startup and the warm-up runs)"
	echo
fi

echo "logs and results: $OUT_DIR"
if [ "$FAILED" = 1 ]; then
	KEEP=1
	exit 2
fi
[ "$MISMATCH" = 1 ] && exit 1
exit 0
