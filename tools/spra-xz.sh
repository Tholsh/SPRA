#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
set -euo pipefail

usage() {
    printf '%s\n' 'usage: spra-xz.sh encode [--bits 4|6] [-z] N [INPUT|-] [-- XZ_ARGS...]
       spra-xz.sh encode-raw [--bits 4|6] [-z] N [INPUT|-]
       spra-xz.sh decode [--strict] [--max-symbols COUNT] [INPUT|-] [-- XZ_ARGS...]

Encode a word to SPRA 0.2 plus xz, or decode a .spra.xz archive.
Both commands write only their result to stdout and preserve input files.
Omitted INPUT or '-' reads stdin. Pass xz options after '--', e.g. -- -6e;
they replace the encode default, -T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0.
SPRA_CODEC overrides the codec executable; otherwise build/ and the repository
root are searched. Build tools/reduced_codec.cpp first; see docs/corpus.md.'
    printf '%s\n' 'Encoding measures length, tuple count and width first, then streams with a complete header.
encode-raw writes SPRA without xz. Pass --length COUNT --tuples COUNT --bits 4|6
to skip measurement. Otherwise stdin is saved temporarily so it can be read twice.'
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
if [[ ${1:-} != encode && ${1:-} != encode-raw && ${1:-} != decode ]]; then
    usage >&2
    exit 1
fi
operation=$1
shift
codec_args=()
while (($#)) && [[ $1 != -- ]]; do
    codec_args+=("$1")
    shift
done
if (($#)); then shift; fi
xz_args=("$@")
# SPRA bytes are tokens, not text: literal context and position bits only cost
# space, and a large dictionary reaches repeats far back. One thread, one block.
default_xz_args=(-T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0)
# Decode options go to the codec; at most one other argument names the input.
decode_args=()
input=-
has_input=false
if [[ $operation == decode ]]; then
    for ((i = 0; i < ${#codec_args[@]}; ++i)); do
        arg=${codec_args[i]}
        if [[ $arg == --max-symbols ]]; then
            decode_args+=("$arg")
            if ((i + 1 < ${#codec_args[@]})); then
                decode_args+=("${codec_args[i + 1]}")
                ((++i))
            fi
        elif [[ $arg == --max-symbols=* || $arg == --strict ]]; then
            decode_args+=("$arg")
        elif ! $has_input; then
            input=$arg
            has_input=true
        else
            usage >&2
            exit 1
        fi
    done
fi

script_path=${BASH_SOURCE[0]//\\//}
if [[ $script_path == */* ]]; then
    script_dir=$(cd -- "${script_path%/*}" && pwd)
else
    script_dir=$PWD
fi
codec=${SPRA_CODEC:-}
if [[ -z $codec ]]; then
    for candidate in "$script_dir/../build/reduced_codec"{,.exe} \
                     "$script_dir/../reduced_codec"{,.exe} \
                     "$script_dir/../build/reduced-codec"{,.exe} \
                     "$script_dir/../reduced-codec"{,.exe}; do
        if [[ -x $candidate ]]; then
            codec=$candidate
            break
        fi
    done
fi
if [[ -z $codec ]]; then
    printf '%s\n' 'spra-xz: codec not found; build tools/reduced_codec.cpp or set SPRA_CODEC (see docs/corpus.md).' >&2
    exit 1
fi

if [[ $operation == encode || $operation == encode-raw ]]; then
    # A bounded-memory first pass supplies all fields needed before a pipe body.
    measure_args=(); supplied_length=; supplied_tuples=; has_bits=false
    input_index=-1; positional=0
    for ((i=0; i<${#codec_args[@]}; ++i)); do
        arg=${codec_args[i]}
        case $arg in
            --length|--tuples)
                ((i+1 < ${#codec_args[@]})) || { usage >&2; exit 1; }
                value=${codec_args[i+1]}; i=$((i+1))
                [[ $value =~ ^[0-9]+$ ]] || { echo "$arg requires a count" >&2; exit 1; }
                if [[ $arg == --length ]]; then
                    [[ -z $supplied_length ]] || { echo 'duplicate --length' >&2; exit 1; }; supplied_length=$value
                else
                    [[ -z $supplied_tuples ]] || { echo 'duplicate --tuples' >&2; exit 1; }; supplied_tuples=$value
                fi;;
            --length=*) [[ -z $supplied_length && ${arg#*=} =~ ^[0-9]+$ ]] || exit 1; supplied_length=${arg#*=};;
            --tuples=*) [[ -z $supplied_tuples && ${arg#*=} =~ ^[0-9]+$ ]] || exit 1; supplied_tuples=${arg#*=};;
            --bits)
                ((i+1 < ${#codec_args[@]})) || { usage >&2; exit 1; }
                measure_args+=("$arg" "${codec_args[i+1]}"); i=$((i+1)); has_bits=true;;
            --bits=*) measure_args+=("$arg"); has_bits=true;;
            -z|--zero-based) measure_args+=("$arg");;
            *)
                if [[ $arg == - || $arg != -* ]]; then
                    positional=$((positional+1))
                    if ((positional == 2)); then input_index=${#measure_args[@]}; fi
                fi
                measure_args+=("$arg");;
        esac
    done
    if [[ -z $supplied_length || -z $supplied_tuples || $has_bits == false ]]; then
        if ((input_index < 0)) || [[ ${measure_args[input_index]} == - ]]; then
            raw_input=$(mktemp "${TMPDIR:-/tmp}/spra-input.XXXXXXXX")
            trap 'rm -f -- "$raw_input"' EXIT
            cat > "$raw_input"
            if ((input_index < 0)); then measure_args+=("$raw_input"); else measure_args[input_index]=$raw_input; fi
        fi
        metadata=$("$codec" measure "${measure_args[@]}")
        read -r measured_length measured_tuples measured_bits <<< "$metadata"
        codec_args=("${measure_args[@]}" --length "${supplied_length:-$measured_length}" --tuples "${supplied_tuples:-$measured_tuples}")
        if ! $has_bits; then codec_args+=(--bits "$measured_bits"); fi
    fi
    if [[ $operation == encode-raw ]]; then
        "$codec" encode "${codec_args[@]}"
        exit
    fi
    if ((${#xz_args[@]} == 0)); then xz_args=("${default_xz_args[@]}"); fi
    "$codec" encode "${codec_args[@]}" | xz -c "${xz_args[@]}"
else
    xz -dc "${xz_args[@]}" -- "$input" | "$codec" decode ${decode_args[@]+"${decode_args[@]}"}
fi
