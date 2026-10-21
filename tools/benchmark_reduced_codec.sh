#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
set -euo pipefail
unset XZ_OPT XZ_DEFAULTS
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
codec=$root/build/reduced-codec
[[ -f $codec.exe ]] && codec+=.exe
if [[ ! -f $codec ]]; then codec=$root/reduced-codec; [[ ! -f $codec.exe ]] || codec+=.exe; fi
baseline= prefix= zero=false
bits=(4 6); xargs=(); positional=()
fail() { printf 'benchmark-reduced-codec: %s\n' "$*" >&2; exit 1; }
trap 'echo "benchmark-reduced-codec: command failed (line $LINENO)" >&2' ERR
while (($#)); do
    case $1 in
        -h|--help) echo 'Usage: benchmark_reduced_codec.sh N INPUT [--codec PATH] [--baseline-codec PATH] [--xz-arg=OPTION] [-z] [--bits 4 6] [--prefix-bytes N]'; exit 0;;
        --codec|--baseline-codec|--prefix-bytes|--xz-arg)
            (($#>=2)) || fail "missing value for $1"
            case $1 in --codec) codec=$2;; --baseline-codec) baseline=$2;; --prefix-bytes) prefix=$2;; --xz-arg) xargs+=("$2");; esac; shift 2;;
        --xz-arg=*) xargs+=("${1#*=}"); shift;;
        -z|--zero-based) zero=true; shift;;
        --bits) shift; bits=(); while (($#)) && [[ $1 == 4 || $1 == 6 ]]; do bits+=("$1"); shift; done; ((${#bits[@]})) || fail '--bits needs 4 and/or 6';;
        --) shift; positional+=("$@"); break;;
        -*) fail "unknown option: $1";;
        *) positional+=("$1"); shift;;
    esac
done
((${#positional[@]}==2)) || fail 'N and INPUT are required'
n=${positional[0]}; input=${positional[1]}
[[ $n =~ ^[0-9]+$ ]] && ((n>=1 && n<=32)) || fail 'N must be 1..32'
[[ -z $prefix || $prefix =~ ^[1-9][0-9]*$ ]] || fail 'prefix must be positive'
work=$(mktemp -d "${TMPDIR:-/tmp}/spra-benchmark.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
# Prefix selection counts original bytes, before whitespace removal. Capture
# pipeline statuses so intentional head termination does not hide other errors.
if [[ ${input,,} == *.xz ]]; then
    if [[ -n $prefix ]]; then
        set +e
        xz -dc -- "$input" | head -c "$prefix" > "$work/source"
        statuses=("${PIPESTATUS[@]}")
        set -e
        [[ ${statuses[1]} == 0 && ( ${statuses[0]} == 0 || ${statuses[0]} == 141 ) ]] || fail 'cannot read compressed prefix'
    else xz -dc -- "$input" > "$work/source"; fi
elif [[ -n $prefix ]]; then head -c "$prefix" -- "$input" > "$work/source"
else cp -- "$input" "$work/source"; fi
LC_ALL=C tr -d ' \t\r\n\v\f' < "$work/source" > "$work/word"
size() { wc -c < "$1" | tr -d '[:space:]'; }
hash() { local result; result=$(sha256sum < "$1"); printf '%s' "${result%% *}"; }
quote() {
    local s=$1 c i value
    printf '"'
    LC_ALL=C
    for ((i=0;i<${#s};i++)); do
        c=${s:i:1}
        case $c in '"') printf '\\"';; '\') printf '\\\\';;
            *) printf -v value '%d' "'$c"; if ((value<32)); then printf '\\u%04x' "$value"; else printf '%s' "$c"; fi;; esac
    done
    printf '"'
}
word_size=$(size "$work/word"); word_hash=$(hash "$work/word")
xz "${xargs[@]}" -c < "$work/word" > "$work/direct.xz"
xz -dc "$work/direct.xz" > "$work/direct-restored"
cmp "$work/word" "$work/direct-restored"
measure() {
    local tool=$1 b comma= packed total body logical version i
    local -a flags=() header=()
    $zero && flags=(-z)
    printf '['
    for b in "${bits[@]}"; do
        printf '%s{"bits_per_field":%s,' "$comma" "$b"; comma=,
        packed=$work/packed
        if ! "$tool" encode --bits "$b" "${flags[@]}" "$n" "$work/word" > "$packed" 2> "$work/error"; then
            printf '"fits":false,"error":'; quote "$(cat "$work/error")"; printf '}'; continue
        fi
        [[ $(head -c 4 "$packed") == SPRA ]] || fail 'codec did not produce a SPRA header'
        read -r -a header <<< "$(od -An -v -tu1 -N24 "$packed" | tr '\n' ' ')"
        ((${#header[@]}==24)) || fail 'short SPRA header'
        version=${header[4]}; logical=0
        for ((i=0;i<8;i++)); do logical=$((logical | (header[16+i] << (8*i)))); done
        total=$(size "$packed"); body=$((total-24-(word_size<n-1?word_size:n-1)))
        xz "${xargs[@]}" -c < "$packed" > "$work/packed.xz"
        xz -dc "$work/packed.xz" > "$work/unpacked"
        cmp "$packed" "$work/unpacked"
        "$tool" decode "$work/unpacked" > "$work/restored"
        cmp "$work/word" "$work/restored"
        printf '"fits":true,"spra_version_byte":%s,"logical_tuples":%s,"stored_tokens":%s,"spra_bytes":%s,"xz_bytes":%s,"round_trip_sha256_matches":true}' "$version" "$logical" "$((body*8/(2*b)))" "$total" "$(size "$work/packed.xz")"
    done
    printf ']'
}
{
    printf '{"input":'; quote "$input"
    printf ',"n":%s,"zero_based":%s,"prefix_bytes_limit":%s,"word_bytes":%s,"word_sha256":' "$n" "$zero" "${prefix:-null}" "$word_size"; quote "$word_hash"
    printf ',"compressor":"xz","runtime":"bash","settings":'; if ((${#xargs[@]})); then quote 'explicit options'; else quote defaults; fi
    printf ',"compression_arguments":['; comma=; for arg in "${xargs[@]}"; do printf '%s' "$comma"; quote "$arg"; comma=,; done
    printf '],"xz_version":'; version=$(xz --version); quote "${version%%$'\n'*}"
    printf ',"direct_xz_bytes":%s,"direct_round_trip_sha256_matches":true,"layouts":' "$(size "$work/direct.xz")"
    measure "$codec"
    if [[ -n $baseline ]]; then printf ',"baseline_layouts":'; measure "$baseline"; fi
    printf '}\n'
} > "$work/report"
cat "$work/report"
