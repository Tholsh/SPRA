#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Prove binary payload arrives before the producer closes stdin.
set -euo pipefail
unset XZ_OPT XZ_DEFAULTS

# Each case runs in timeout's process group so stalled pipelines and their
# producers are terminated together. Binary data never enters shell variables.
if [[ ${1:-} == --case ]]; then
    shift
    work=$1 mode=$2 input=$3 output=$4 tool=$5 codec=$6
    size=$(wc -c < "$input")
    ((size > 32))
    mkfifo "$work/release"
    exec 3<> "$work/release"
    run_pipeline() {
        case $mode in
            bpe-encode) "$tool" encode ;;
            bpe-decode) "$tool" decode ;;
            xz-encode) xz -c -T1 --block-size=256KiB --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0 ;;
            xz-decode) xz -dc ;;
            raw-encode) "$codec" encode --length 5000000 --tuples 4999998 --bits 4 3 ;;
            raw-decode) "$codec" decode --max-symbols 5000000 ;;
            archive-decode) xz -dc | "$tool" decode | "$codec" decode ;;
            *) echo "Unknown live test: $mode" >&2; return 1 ;;
        esac
    }
    {
        head -c "$((size - 32))" -- "$input"
        # Withhold the final bytes AND EOF until the consumer sees body output.
        IFS= read -r -u 3 ready
        tail -c 32 -- "$input"
    } | run_pipeline | {
        head -c 4096 > "$output"
        if [[ $(wc -c < "$output") -ne 4096 ]]; then
            echo "$mode: no payload before input EOF" >&2
            exit 1
        fi
        printf 'ready\n' >&3
        cat >> "$output"
    }
    exit 0
fi

if (($# != 4)); then
    echo 'usage: bash test_bpe_live.sh BPE_TOOL CODEC ARCHIVE SPRA' >&2
    exit 1
fi
tool=$1 codec=$2 archive=$3 spra=$4
script=$(realpath -- "${BASH_SOURCE[0]}")
work=$(mktemp -d "${TMPDIR:-/tmp}/spra-live-tests.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
live() {
    local mode=$1 input=$2 output=$3
    mkdir "$work/$mode"
    timeout --kill-after=5s 60s bash "$script" --case "$work/$mode" \
        "$mode" "$input" "$output" "$tool" "$codec"
}

live bpe-encode "$spra" "$work/encoded"
live bpe-decode "$work/encoded" "$work/decoded"
cmp "$spra" "$work/decoded"
live xz-encode "$work/encoded" "$work/compressed"
live xz-decode "$work/compressed" "$work/restored"
cmp "$work/encoded" "$work/restored"
head -c 5000000 /dev/zero | tr '\000' 1 > "$work/raw"
live raw-encode "$work/raw" "$work/raw.spra"
live raw-decode "$work/raw.spra" "$work/raw-restored"
cmp "$work/raw" "$work/raw-restored"
live archive-decode "$archive" "$work/word"
"$codec" decode "$spra" | cmp - "$work/word"
echo 'Live SPRA, SPBE, xz, and archive pipelines produced payload before EOF and round-tripped exactly'
