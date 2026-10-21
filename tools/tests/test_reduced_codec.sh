#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
unset XZ_OPT XZ_DEFAULTS
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/spra-codec-tests.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
"$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/tests/test_reduced_codec.cpp" -o "$work/vectors.exe"
"$work/vectors.exe"
"$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/reduced_codec.cpp" -o "$work/codec with spaces.exe"
export SPRA_CODEC="$work/codec with spaces.exe"
wrapper=$root/tools/spra-xz.sh
printf '1 1111\n' > "$work/input word.txt"
printf 11111 > "$work/expected"
for bits in 4 6; do
    bash "$wrapper" encode --bits "$bits" 3 "$work/input word.txt" -- -6e --check=crc32 > "$work/packed word.xz"
    bash "$wrapper" decode "$work/packed word.xz" -- --memlimit-decompress=64MiB > "$work/restored"
    cmp "$work/expected" "$work/restored"
    head -c -1 "$work/packed word.xz" > "$work/truncated.xz"
    if bash "$wrapper" decode "$work/truncated.xz" > /dev/null 2>&1; then echo 'truncated xz accepted' >&2; exit 1; fi
done
for args in 'unknown' 'decode one two' 'encode 3 -- --not-an-xz-option'; do
    if bash "$wrapper" $args < /dev/null > /dev/null 2>&1; then echo "bad wrapper arguments accepted: $args" >&2; exit 1; fi
done
printf bad > "$work/bad"
if bash "$wrapper" encode 3 "$work/bad" > "$work/out" 2>/dev/null; then exit 1; fi
if "$SPRA_CODEC" encode 3 "$work/bad" > "$work/out" 2>/dev/null; then exit 1; fi
[[ ! -s $work/out ]]
for args in '' 'unknown' 'encode' 'encode 0' 'encode 33' 'encode 3foo' 'encode --bits 5 3' 'encode --bits=6foo 3' 'encode --bits 4 --bits=6 3' 'decode --bits 6' 'encode -z --zero-based 3' 'decode -z' 'decode a b' 'encode 3 --unknown'; do
    if "$SPRA_CODEC" $args < /dev/null > "$work/out" 2> "$work/error"; then echo "invalid codec arguments accepted: $args" >&2; exit 1; fi
    [[ ! -s $work/out && -s $work/error ]]
done
for n in 5 6 7 8 9; do
    case $n in
        5) cp "$root/words/5/153/153-recursive.txt" "$work/source";;
        6) tar -xOJf "$root/words/6/872/872-treelike.tar.xz" '872-treelike/872-00001.txt' > "$work/source";;
        7) cp "$root/words/7/5913/5913-palindromic.txt" "$work/source";;
        8) cp "$root/words/8/46181/superperm8_46181_1.txt" "$work/source";;
        9) bash "$wrapper" decode "$root/words/9/408731/superperm9_408731_19_canonical.spra.xz" > "$work/source";;
    esac
    tr -d ' \t\r\n\v\f' < "$work/source" > "$work/expected"
    for bits in 4 6; do
        bash "$wrapper" encode --bits "$bits" "$n" "$work/source" > "$work/packed.xz"
        bash "$wrapper" decode "$work/packed.xz" > "$work/restored"
        cmp "$work/expected" "$work/restored"
    done
done
bash "$root/tools/benchmark_reduced_codec.sh" 3 "$work/input word.txt" --codec "$SPRA_CODEC" --baseline-codec "$SPRA_CODEC" --xz-arg=-6e > "$work/report.json"
# PowerShell's JSON parser checks the actual report on Windows; jq elsewhere.
if command -v powershell.exe >/dev/null 2>&1; then
    export SPRA_TEST_REPORT=$(cygpath -w "$work/report.json")
    powershell.exe -NoProfile -Command '$r=Get-Content -Raw $env:SPRA_TEST_REPORT|ConvertFrom-Json; if (($r.layouts|ConvertTo-Json -Compress) -ne ($r.baseline_layouts|ConvertTo-Json -Compress)) {exit 1}; foreach($l in $r.layouts){if($l.logical_tuples -ne 3 -or $l.stored_tokens -ne 2 -or !$l.round_trip_sha256_matches){exit 1}}'
elif command -v jq >/dev/null 2>&1; then
    jq -e '.layouts == .baseline_layouts and (.layouts | all(.logical_tuples == 3 and .stored_tokens == 2 and .round_trip_sha256_matches))' "$work/report.json" > /dev/null
else echo 'Install jq to validate benchmark JSON' >&2; exit 1; fi
xz -c "$work/input word.txt" > "$work/input.xz"
bash "$root/tools/benchmark_reduced_codec.sh" --prefix-bytes 3 3 "$work/input.xz" --codec "$SPRA_CODEC" --bits 6 > "$work/prefix.json"
grep -q '"word_bytes":2' "$work/prefix.json"
echo 'Codec CLI, xz wrapper, corpus and benchmark checks passed'
