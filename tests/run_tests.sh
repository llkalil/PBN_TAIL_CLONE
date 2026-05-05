#!/usr/bin/env bash
# tests/run_tests.sh — test suite for the tail clone
# Run from the repository root: make test  OR  bash tests/run_tests.sh

set -euo pipefail

TAIL="./tail"
PASS=0
FAIL=0
TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

run_test() {
    local desc="$1"
    local expected="$2"
    local actual="$3"
    if [ "$expected" = "$actual" ]; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc"
        echo "    expected: $(echo "$expected" | head -5 | sed 's/^/      /')"
        echo "    actual:   $(echo "$actual"   | head -5 | sed 's/^/      /')"
        FAIL=$((FAIL + 1))
    fi
}

# Create a reusable test file (lines 1..20)
FILE20="$TMPDIR_TEST/twenty.txt"
seq 1 20 > "$FILE20"

# Create a small file (5 lines)
FILE5="$TMPDIR_TEST/five.txt"
printf "alpha\nbeta\ngamma\ndelta\nepsilon\n" > "$FILE5"

# Create an empty file
FILE_EMPTY="$TMPDIR_TEST/empty.txt"
: > "$FILE_EMPTY"

# ---------------------------------------------------------------------------
echo ""
echo "=== Basic line tests ==="

run_test "default last 10 lines of 20-line file" \
    "$(seq 11 20)" \
    "$($TAIL "$FILE20")"

run_test "-n 3 prints last 3 lines" \
    "$(printf '18\n19\n20')" \
    "$($TAIL -n 3 "$FILE20")"

run_test "-n 1 prints last line only" \
    "20" \
    "$($TAIL -n 1 "$FILE20")"

run_test "-n 0 prints nothing" \
    "" \
    "$($TAIL -n 0 "$FILE20")"

run_test "-n 20 prints entire 20-line file" \
    "$(seq 1 20)" \
    "$($TAIL -n 20 "$FILE20")"

run_test "-n 25 prints entire file when count > lines" \
    "$(seq 1 20)" \
    "$($TAIL -n 25 "$FILE20")"

run_test "empty file produces no output" \
    "" \
    "$($TAIL "$FILE_EMPTY")"

# ---------------------------------------------------------------------------
echo ""
echo "=== From-start line tests (-n +NUM) ==="

run_test "-n +1 prints entire file" \
    "$(seq 1 20)" \
    "$($TAIL -n +1 "$FILE20")"

run_test "-n +5 prints from line 5 to end" \
    "$(seq 5 20)" \
    "$($TAIL -n +5 "$FILE20")"

run_test "-n +20 prints last line only" \
    "20" \
    "$($TAIL -n +20 "$FILE20")"

run_test "-n +21 prints nothing (past EOF)" \
    "" \
    "$($TAIL -n +21 "$FILE20")"

# ---------------------------------------------------------------------------
echo ""
echo "=== Byte tests (-c NUM) ==="

# Last 3 bytes of "alpha\nbeta\ngamma\ndelta\nepsilon\n" → "on\n"
run_test "-c 3 prints last 3 bytes" \
    "$(printf 'on\n')" \
    "$($TAIL -c 3 "$FILE5")"

run_test "-c 0 prints nothing" \
    "" \
    "$($TAIL -c 0 "$FILE5")"

# ---------------------------------------------------------------------------
echo ""
echo "=== From-start byte tests (-c +NUM) ==="

# File5: "alpha\nbeta\ngamma\ndelta\nepsilon\n" — byte +7 starts at 'b' in 'beta'
run_test "-c +7 starts from 7th byte" \
    "$(printf 'beta\ngamma\ndelta\nepsilon\n')" \
    "$($TAIL -c +7 "$FILE5")"

run_test "-c +1 prints entire file" \
    "$(printf 'alpha\nbeta\ngamma\ndelta\nepsilon\n')" \
    "$($TAIL -c +1 "$FILE5")"

# ---------------------------------------------------------------------------
echo ""
echo "=== stdin tests ==="

run_test "stdin default last 10 lines" \
    "$(seq 11 20)" \
    "$(seq 1 20 | $TAIL)"

run_test "stdin -n 2" \
    "$(printf '4\n5')" \
    "$(seq 1 5 | $TAIL -n 2)"

run_test "stdin -n +3 from line 3" \
    "$(printf '3\n4\n5')" \
    "$(seq 1 5 | $TAIL -n +3)"

run_test "stdin '-' as file name reads stdin" \
    "$(printf '4\n5')" \
    "$(seq 1 5 | $TAIL -n 2 -)"

# ---------------------------------------------------------------------------
echo ""
echo "=== Header tests ==="

run_test "-v forces header for single file" \
    "$(printf '==> %s <==\n19\n20' "$FILE20")" \
    "$($TAIL -v -n 2 "$FILE20")"

run_test "-q suppresses headers for multiple files" \
    "$(printf '19\n20\n19\n20')" \
    "$($TAIL -q -n 2 "$FILE20" "$FILE20")"

run_test "multiple files get headers by default" \
    "$(printf '==> %s <==\ndelta\nepsilon\n\n==> %s <==\ndelta\nepsilon' "$FILE5" "$FILE5")" \
    "$($TAIL -n 2 "$FILE5" "$FILE5")"

# ---------------------------------------------------------------------------
echo ""
echo "=== Error handling ==="

run_test "non-existent file exits with error code 1" \
    "1" \
    "$($TAIL /tmp/this_file_does_not_exist_xyz 2>/dev/null; echo $?)"

# ---------------------------------------------------------------------------
echo ""
echo "Results: $PASS passed, $FAIL failed."
echo ""
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
