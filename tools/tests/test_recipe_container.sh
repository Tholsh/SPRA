#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Recipe containers: decode the example words and check length and SHA-256 of the letters against the values below
# (not only against the container header); refusal of a changed generator, a damaged header and a truncated file;
# the SPRA fallback; the plan coder round trip.
#
# usage: bash tools/tests/test_recipe_container.sh [--slow]
#   --slow (or RECIPE_SLOW=1) also decodes n = 13 (about 3 min, 2 GB of memory, 14 GB of temporary disk) and
#   n = 14 (about 4 min, 0.5 GB; the 94 GB word is hashed as a stream, not stored).
# Needs Python 3 (PYTHON, default python3), a C compiler for gen14 (CC, default gcc), a C++20 compiler for the SPRA
# fallback (CXX, default g++), xz and sha256sum. RECIPE_WORK selects the folder for temporary files.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
slow=${RECIPE_SLOW:-0}
[[ ${1:-} == --slow ]] && slow=1
py=${PYTHON:-python3}
work=$(mktemp -d "${RECIPE_WORK:-${TMPDIR:-/tmp}}/spra-recipe-tests.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
rc() { "$py" -E -s "$root/tools/recipe/recipe.py" "$@"; }
status() { set +e; "$@" > "$work/out.log" 2>&1; local s=$?; set -e; echo "$s"; }
fail() { echo "FAIL: $*" >&2; cat "$work/out.log" >&2 2>/dev/null || true; exit 1; }

# name n length sha256 of the letters (no line feed), alphabet 0123456789ABCD
words=(
    "10/4034855 10 4034855 542a3778662f972a4ea7b9461a9334775f31303a0d8b1a7913169d8beea444ff"
    "11/43930578 11 43930578 f7d3122f350d56d41945961f9abf36453420bd3068a3941b424f9c0447c7aef5"
    "12/522736969 12 522736969 eb4861f06f9eaa652bf355ff5e3eac2a3080c1e7dee72f527e517c949b2d6918"
    "13/6747801835 13 6747801835 8905fc686861187923accc190c67773880fcee3aa669ba63c9ca80f778879387"
    "14/93904804425 14 93904804425 3b1470a204854ce09654b9d9a811b8455308212b29585accc29d0263e423ab6f"
)
file_of() { local d=$1; echo "$root/words/$d/superpermutation-${d%%/*}-${d##*/}.sprc"; }

for entry in "${words[@]}"; do
    read -r d n len sha <<< "$entry"
    f=$(file_of "$d")
    [[ -f $f ]] || fail "missing $f"
    rc info "$f" > "$work/info.txt" || fail "info $d"
    grep -q "^sha256      $sha\$" "$work/info.txt" || fail "header hash of $d"
    grep -q "^word        n = $n, alphabet .*, $len letters\$" "$work/info.txt" || fail "header length of $d"
    if ((n >= 13)); then
        if ((slow)); then
            # the decoder compares with the header, which was just compared with the values above
            s=$(status rc decode "$f" --work "$work")
            [[ $s == 0 ]] || fail "decode $d (exit $s)"
            grep -q "^ok: $len letters, SHA-256 $sha" "$work/out.log" || fail "decode $d"
            echo "n = $n: $len letters, SHA-256 matches (slow)"
        else
            echo "n = $n: header checked; decode skipped (run with --slow)"
        fi
        continue
    fi
    rc decode "$f" -o "$work/word.txt" --work "$work" --quiet || fail "decode $d"
    [[ $(wc -c < "$work/word.txt") -eq $len ]] || fail "length of $d"
    [[ $(sha256sum < "$work/word.txt" | cut -c1-64) == "$sha" ]] || fail "SHA-256 of $d"
    rm -f "$work/word.txt"
    echo "n = $n: $len letters, SHA-256 matches"
done

f10=$(file_of 10/4034855)
# a damaged expected hash: the word is decoded and refused (exit 3), no output file is left
cp "$f10" "$work/badsha.sprc"
printf '\000' | dd of="$work/badsha.sprc" bs=1 seek=25 conv=notrunc 2>/dev/null
s=$(status rc decode "$work/badsha.sprc" -o "$work/bad.txt" --work "$work")
[[ $s == 3 && ! -e $work/bad.txt ]] || fail "damaged hash gave exit $s"
# truncated container and wrong magic: refused before anything runs (exit 2)
head -c 100 "$f10" > "$work/short.sprc"
[[ $(status rc decode "$work/short.sprc" --work "$work") == 2 ]] || fail "truncated container accepted"
{ printf 'SPRX'; tail -c +5 "$f10"; } > "$work/magic.sprc"
[[ $(status rc decode "$work/magic.sprc" --work "$work") == 2 ]] || fail "wrong magic accepted"

# a reader whose generator source differs from the hash in the container refuses it (exit 2) ...
mkdir -p "$work/copy/tools"
cp -r "$root/tools/recipe" "$work/copy/tools/recipe"
rm -rf "$work/copy/tools/recipe/gen/pantone_py/__pycache__"
printf '# changed\n' >> "$work/copy/tools/recipe/gen/pantone_py/gcore.py"
s=$(status "$py" -E -s "$work/copy/tools/recipe/recipe.py" decode "$f10" --work "$work")
[[ $s == 2 ]] && grep -q 'pantone-py/1: source hash' "$work/out.log" || fail "changed generator gave exit $s"

# ... and points to the SPRA fallback when there is one; the fallback here is the corpus file of the same word
spra=$root/words/10/4034855/superpermutation-10-4034855.spra.xz
if [[ -f $spra ]]; then
    "${CXX:-g++}" -std=c++20 -O2 "$root/tools/reduced_codec.cpp" -o "$work/reduced-codec.exe"
    export SPRA_CODEC=$work/reduced-codec.exe
    rc add-fallback "$f10" "$spra" "$work/fb.sprc"
    rc extract-fallback "$work/fb.sprc" "$work/fb.spra.xz"
    cmp "$spra" "$work/fb.spra.xz" || fail "fallback bytes"
    s=$(status "$py" -E -s "$work/copy/tools/recipe/recipe.py" decode "$work/fb.sprc" --work "$work")
    [[ $s == 2 ]] && grep -q -- '--fallback' "$work/out.log" || fail "refusal without fallback hint"
    "$py" -E -s "$work/copy/tools/recipe/recipe.py" decode "$work/fb.sprc" --fallback -o "$work/fb.txt" \
        --work "$work" --quiet || fail "fallback decode"
    [[ $(sha256sum < "$work/fb.txt" | cut -c1-64) == 542a3778662f972a4ea7b9461a9334775f31303a0d8b1a7913169d8beea444ff ]] \
        || fail "fallback SHA-256"
    rc decode "$work/fb.sprc" --work "$work" --quiet || fail "generators with a fallback present"
    echo "fallback: SPRA corpus word decodes to the same letters; changed generator refused"
else
    echo "changed generator refused (no SPRA corpus file for the fallback check)"
fi

# plan coder: byte-exact round trip with CRLF lines, verbatim lines, a bare integer column and no final newline
printf 'TRAILSEARCH-PLAN 10 4036801 3\nO 154 0 3 0\r\nO 12 7 2 1\n# note\n17\nS 1 2 3\nS 4 5\nx' > "$work/p.txt"
rc planenc "$work/p.txt" "$work/p.plc"
rc plandec "$work/p.plc" "$work/p2.txt"
cmp "$work/p.txt" "$work/p2.txt" || fail "plan round trip"
echo 'Recipe container decode, refusal, fallback and plan coder checks passed'
