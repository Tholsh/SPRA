#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
unset XZ_OPT XZ_DEFAULTS
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/spra-bpe-tests.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=${CXX:-g++}
"$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/bpe_layer.cpp" -o "$work/bpe with spaces"
"$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/reduced_codec.cpp" -o "$work/codec"
"$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/tests/test_bpe_stream.cpp" -o "$work/stream-tests"
"$work/stream-tests"
export SPRA_BPE="$work/bpe with spaces" SPRA_CODEC="$work/codec"
bpe=$SPRA_BPE
wrapper=$root/tools/spra-bpe-xz.sh
checks=0
hex() { printf "$(printf '%s' "$1" | sed 's/ //g; s/../\\x&/g')"; }
expect_fail() { # FILE MESSAGE [ARGS...]: decode must fail with MESSAGE and write nothing
    local file=$1 message=$2
    shift 2
    if "$bpe" decode "$@" "$file" > "$work/out" 2> "$work/error"; then echo "accepted: $message" >&2; exit 1; fi
    [[ ! -s $work/out ]] || { echo "output before error: $message" >&2; exit 1; }
    grep -q -- "$message" "$work/error" || { echo "expected '$message', got: $(cat "$work/error")" >&2; exit 1; }
    checks=$((checks + 1))
}
# Independent vectors (docs/bpe_layer.md): 123121321 and 11111 at N=3, and a
# body with one pair rule (the layer does not check the word itself).
hdr_a='53505241 02 44 00 03 0900000000000000 0100000000000000 0001'
spra_a="$hdr_a 31"
spbe_a="53504245 01 $hdr_a 01 0301 01 00 01 00"
hdr_b='53505241 02 44 00 03 0500000000000000 0300000000000000 0000'
spra_b="$hdr_b 0220"
spbe_b="53504245 01 $hdr_b 01 0002 03 00 01 00"
hdr_c='53505241 02 44 00 03 1200000000000000 0400000000000000 0001'
spra_c="$hdr_c 02310231"
spbe_c="53504245 01 $hdr_c 02 0002 01 0301 01 01 0001 02 0202"
for v in a b c; do
    spra=spra_$v spbe=spbe_$v
    hex "${!spra}" > "$work/$v.spra"
    hex "${!spbe}" > "$work/$v.spbe"
    expected=${!spbe}
    hex "${expected/53504245 01/53504245 02} 00" > "$work/$v.expected-stream"
    "$bpe" encode --min-count 2 "$work/$v.spra" | cmp - "$work/$v.expected-stream"
    "$bpe" encode --whole-file --min-count 2 "$work/$v.spra" | cmp - "$work/$v.spbe"
    "$bpe" decode "$work/$v.spbe" | cmp - "$work/$v.spra"
    for count in 0 1 3 1024 --min-count=9; do
        [[ $count == --* ]] && args=("$count") || args=(--min-count "$count")
        "$bpe" encode "${args[@]}" < "$work/$v.spra" > "$work/count.spbe"
        "$bpe" decode - < "$work/count.spbe" | cmp - "$work/$v.spra"
    done
    checks=$((checks + 7))
done
# Many distinct run types and a very long run exercise bounded training,
# inline definitions, full-dictionary escapes and split-run recombination.
"$work/stream-tests" --fixture > "$work/many.spra"
for train in 1 64 65536; do
    "$bpe" encode --train-runs "$train" "$work/many.spra" > "$work/many.spbe"
    "$bpe" decode "$work/many.spbe" | cmp - "$work/many.spra"
done
# Default output is streaming SPBE; whole-file vectors still decode identically.
for v in a b c; do
    "$bpe" encode --train-runs 1 --min-count 2 "$work/$v.spra" > "$work/$v.stream"
    [[ $(od -An -tu1 -j4 -N1 "$work/$v.stream" | tr -d ' ') == 2 ]]
    "$bpe" decode "$work/$v.stream" | cmp - "$work/$v.spra"
    bytes=$(wc -c < "$work/$v.stream")
    for ((cut=0;cut<bytes;cut++)); do
        head -c "$cut" "$work/$v.stream" > "$work/stream-cut"
        if "$bpe" decode "$work/stream-cut" > /dev/null 2>&1; then echo 'truncated stream accepted' >&2;exit 1;fi
    done
    cat "$work/$v.stream" <(printf x) > "$work/stream-extra"
    if "$bpe" decode "$work/stream-extra" > /dev/null 2>&1; then exit 1;fi
done
# These streams exercise inline run definitions and raw-run escape records.
for records in '02 0002 02 01 01 01 00' '03 0002 02 00'; do
    hex "53504245 02 $hdr_b 01 0002 01 00 01 00 $records" > "$work/inline.spbe"
    "$bpe" decode "$work/inline.spbe" | cmp - "$work/b.spra"
done
for records in '04' '01 00' '02 0002 00 00' '03 0002 04 00' '01 81808001'; do
    hex "53504245 02 $hdr_b 01 0002 01 00 01 00 $records" > "$work/invalid-stream.spbe"
    if "$bpe" decode "$work/invalid-stream.spbe" > /dev/null 2>&1; then echo 'bad streaming record accepted' >&2;exit 1;fi
done
for args in 'encode --train-runs 0' 'encode --train-runs 1048577' 'encode --legacy' 'encode --whole-file --train-runs 1' 'encode --whole-file --whole-file' 'decode --whole-file' 'encode --train-runs 1 --train-runs 2' 'decode --legacy'; do
    if "$bpe" $args < /dev/null > /dev/null 2>&1; then echo "invalid streaming option: $args" >&2;exit 1;fi
done
hex "53504245 02 $hdr_c 02 0002 01 0301 01 00 04 00010001 00" > "$work/c0.spbe"
"$bpe" encode "$work/c.spra" | cmp - "$work/c0.spbe"
hex "53504245 01 $hdr_c 02 0002 01 0301 01 00 04 00010001" > "$work/c0.whole.spbe"
"$bpe" encode --whole-file "$work/c.spra" | cmp - "$work/c0.whole.spbe"
# Non-canonical RLE (02 10 10) comes back as the canonical body 02 20.
hex '53505241 02 44 00 03 0500000000000000 0300000000000000 0000 021010' > "$work/split.spra"
"$bpe" encode "$work/split.spra" | "$bpe" decode | cmp - "$work/b.spra"
# Malformed SPBE files.
size=$(wc -c < "$work/c.spbe")
for ((cut = 0; cut < size; ++cut)); do
    head -c "$cut" "$work/c.spbe" > "$work/cut.spbe"
    expect_fail "$work/cut.spbe" 'spra-bpe: '
done
cat "$work/c.spbe" <(printf x) > "$work/extra.spbe"
expect_fail "$work/extra.spbe" 'extra bytes'
for case in "magic|53504246 01 $hdr_a 01 0301 01 00 01 00|not an SPBE file" \
            "mode|53504245 03 $hdr_a 01 0301 01 00 01 00|unsupported SPBE mode" \
            "runs|53504245 01 $hdr_a 02 0301 01 0302 01 00 01 00|more distinct runs" \
            "offset|53504245 01 $hdr_a 01 0308 01 00 01 00|does not fit" \
            "marker|53504245 01 $hdr_a 01 0100 01 00 01 00|offset 0" \
            "count0|53504245 01 $hdr_a 01 0301 00 00 01 00|run count" \
            "count2|53504245 01 $hdr_a 01 0301 02 00 01 00|run count" \
            "rules|53504245 01 $hdr_c 02 0002 01 0301 01 ff|more than 256 runs and rules" \
            "later|53504245 01 $hdr_c 02 0002 01 0301 01 01 0002 01 02|refers to a later symbol" \
            "body|53504245 01 $hdr_a 01 0301 01 00 01 01|neither a run nor a rule" \
            "short|53504245 01 $hdr_a 01 0301 01 00 00|do not reach" \
            "long|53504245 01 $hdr_a 01 0301 01 00 02 0000|exceed the logical" \
            "v1|53504245 01 53505241 01 44 00 03 0900000000000000 0100000000000000 0001 01 0301 01 00 01 00|version must be 0x02" \
            "varint|53504245 01 $hdr_a ffffffffffffffffff7f|varint overflow"; do
    IFS='|' read -r name bytes message <<< "$case"
    hex "$bytes" > "$work/$name.spbe"
    expect_fail "$work/$name.spbe" "$message"
done
expect_fail "$work/a.spbe" 'exceeds --max-symbols 8' --max-symbols 8
"$bpe" decode --max-symbols=9 "$work/a.spbe" | cmp - "$work/a.spra"
hex '53504245 01 53505241 02 44 00 01 ffffffffffffffff 0000000000000000 00 00 00' > "$work/long.spbe"
expect_fail "$work/long.spbe" 'exceeds N \* N! = 1'
# Malformed SPRA input to encode.
for case in "53505241 01 44 00 03 0900000000000000 0100000000000000 0001 31|version must be 0x02" \
            "53505241 02 44 00 03 0900000000000000 0100000000000000 0001|truncated tuple body" \
            "53505241 02 44 00 03 0900000000000000 0100000000000000 0001 3100|extra bytes" \
            "53505241 02 44 00 03 0500000000000000 0300000000000000 0000 10|no preceding tuple" \
            "53505241 02 66 00 03 0900000000000000 0100000000000000 0001 0c11|padding"; do
    IFS='|' read -r bytes message <<< "$case"
    hex "$bytes" > "$work/bad.spra"
    if "$bpe" encode "$work/bad.spra" > "$work/out" 2> "$work/error"; then echo "encode accepted: $message" >&2; exit 1; fi
    grep -q -- "$message" "$work/error"
    checks=$((checks + 1))
done
for args in '' 'unknown' 'encode --min-count' 'encode --min-count x' 'encode --min-count 1 --min-count 2' \
            'decode --min-count 1' 'encode --max-symbols 5' 'decode --max-symbols x' 'decode a b'; do
    if "$bpe" $args < /dev/null > "$work/out" 2> "$work/error"; then echo "invalid arguments accepted: $args" >&2; exit 1; fi
    [[ ! -s $work/out && -s $work/error ]]
    checks=$((checks + 1))
done
# Words through the wrapper, both widths.
for n in 5 6 7 8 9; do
    case $n in
        5) cp "$root/words/5/153/153-recursive.txt" "$work/source";;
        6) tar -xOJf "$root/words/6/872/872-treelike.tar.xz" '872-treelike/872-00001.txt' > "$work/source";;
        7) cp "$root/words/7/5913/5913-palindromic.txt" "$work/source";;
        8) cp "$root/words/8/46181/superperm8_46181_1.txt" "$work/source";;
        9) xz -dc "$root/words/9/408731/superperm9_408731_19_canonical.spra.xz" | "$SPRA_CODEC" decode > "$work/source";;
    esac
    tr -d ' \t\r\n\v\f' < "$work/source" > "$work/expected"
    for bits in 4 6; do
        bash "$wrapper" encode --bits "$bits" "$n" "$work/source" > "$work/word.bpe.xz"
        bash "$wrapper" decode "$work/word.bpe.xz" > "$work/restored"
        cmp "$work/expected" "$work/restored"
        checks=$((checks + 1))
    done
done
# Corpus archives N=9..11: pack, unpack, identical SPRA bytes.
for archive in "$root"/words/9/*/*.spra.xz "$root"/words/10/*/*.spra.xz "$root"/words/11/*/*.spra.xz; do
    bash "$wrapper" pack "$archive" > "$work/packed.bpe.xz"
    bash "$wrapper" unpack "$work/packed.bpe.xz" > "$work/unpacked.spra"
    xz -dc "$archive" | cmp - "$work/unpacked.spra"
    checks=$((checks + 1))
done
bash "$wrapper" pack - -- -6e --check=crc32 < "$root/words/9/408731/superperm9_408731_19_canonical.spra.xz" > "$work/custom.bpe.xz"
bash "$wrapper" unpack "$work/custom.bpe.xz" -- --memlimit-decompress=64MiB | cmp - <(xz -dc "$root/words/9/408731/superperm9_408731_19_canonical.spra.xz")
bash "$wrapper" pack --whole-file --min-count 2 "$root/words/9/408731/superperm9_408731_19_canonical.spra.xz" > "$work/whole.bpe.xz"
bash "$wrapper" unpack "$work/whole.bpe.xz" | cmp - <(xz -dc "$root/words/9/408731/superperm9_408731_19_canonical.spra.xz")
for args in 'unknown' 'decode one two' 'pack one two'; do
    if bash "$wrapper" $args < /dev/null > /dev/null 2>&1; then echo "bad wrapper arguments accepted: $args" >&2; exit 1; fi
done
archive="$root/words/12/522736969/superpermutation-12-522736969.spra.xz"
bash "$wrapper" pack "$archive" > "$work/live.bpe.xz"
xz -dc "$archive" > "$work/live.spra"
bash "$wrapper" decode --strict "$work/live.bpe.xz" | sha256sum > "$work/live.sha"
xz -dc "$archive" | "$SPRA_CODEC" decode | sha256sum > "$work/source.sha"
cmp "$work/live.sha" "$work/source.sha"
if bash "$wrapper" decode --max-symbols 100 "$work/live.bpe.xz" > /dev/null 2>&1; then exit 1;fi
bash "$root/tools/tests/test_bpe_live.sh" "$SPRA_BPE" "$SPRA_CODEC" "$work/live.bpe.xz" "$work/live.spra"

echo "SPBE layer: $checks reference checks plus streaming, inline-record, corpus and live-pipeline checks passed"
