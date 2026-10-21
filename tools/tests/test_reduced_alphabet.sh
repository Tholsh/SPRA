#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/spra-paint-tests.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/tests/test_reduced_alphabet.cpp" -o "$work/oracle.exe"
"$work/oracle.exe"
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/paint_waste.cpp" -o "$work/paint.exe"
"$work/paint.exe" -s 5 < "$root/words/5/153/153-recursive.txt" > "$work/stats"
grep -q '^distinct_permutations: 120' "$work/stats"
grep -q '^valid_superpermutation: 1' "$work/stats"
# Disconnect the controlling terminal for consent checks.
if [[ ${OS:-} == Windows_NT ]]; then
    : # The C++ oracle uses DETACHED_PROCESS and enforces a timeout.
elif command -v setsid >/dev/null 2>&1; then
    printf 0123456789 > "$work/large-input"
    if setsid "$work/paint.exe" -z 10 < "$work/large-input" > "$work/out" 2> "$work/error"; then exit 1; fi
    [[ ! -s $work/out ]]
    grep -q 'rerun with -s' "$work/error"
else echo 'Detached consent check requires setsid' >&2; exit 1; fi
echo 'Paint corpus and detached consent checks passed'
