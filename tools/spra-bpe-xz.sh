#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jakub Halfar
set -euo pipefail

usage() {
    printf '%s\n' 'usage: spra-bpe-xz.sh encode [BPE_OPTIONS] [--bits 4|6] [-z] N [INPUT|-] [-- XZ_ARGS...]
       spra-bpe-xz.sh decode [--strict] [--max-symbols COUNT] [INPUT|-] [-- XZ_ARGS...]
       spra-bpe-xz.sh pack [BPE_OPTIONS] [INPUT|-] [-- XZ_ARGS...]
       spra-bpe-xz.sh unpack [--max-symbols COUNT] [INPUT|-] [-- XZ_ARGS...]

Streaming SPBE by default (docs/bpe_streaming.md); decoding supports both modes.
BPE_OPTIONS: --train-runs COUNT, --min-count COUNT, or --whole-file (whole-file buffering).
encode: word -> .bpe.xz       decode: .bpe.xz -> word
pack:   .spra.xz -> .bpe.xz   unpack: .bpe.xz -> raw SPRA 0.2 (pipe to xz for .spra.xz)
Every command writes only its result to stdout and preserves input files.
Omitted INPUT or '-' reads stdin. xz options after '--' replace the default
-T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0 when compressing, and go to
xz -d when decompressing. SPRA_CODEC and SPRA_BPE override the executables;
otherwise build/ and the repository root are searched.'
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
case ${1:-} in
    encode|decode|pack|unpack) ;;
    *) usage >&2; exit 1;;
esac
operation=$1
shift
tool_args=()
bpe_args=()
while (($#)) && [[ $1 != -- ]]; do
    if [[ $1 == --whole-file || $1 == --train-runs || $1 == --train-runs=* || $1 == --min-count || $1 == --min-count=* ]]; then
        [[ $operation == encode || $operation == pack ]] || { usage >&2; exit 1; }
        bpe_args+=("$1")
        if [[ $1 == --train-runs || $1 == --min-count ]]; then
            (($# >= 2)) || { usage >&2; exit 1; }
            bpe_args+=("$2"); shift
        fi
    else tool_args+=("$1"); fi
    shift
done
if (($#)); then shift; fi
xz_args=("$@")
default_xz_args=(-T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0)
pack_args=(${xz_args[@]+"${xz_args[@]}"})
if ((${#pack_args[@]} == 0)); then pack_args=("${default_xz_args[@]}"); fi

script_path=${BASH_SOURCE[0]//\\//}
if [[ $script_path == */* ]]; then
    script_dir=$(cd -- "${script_path%/*}" && pwd)
else
    script_dir=$PWD
fi
find_tool() {
    local candidate
    for candidate in "$script_dir/../build/$1"{,.exe} "$script_dir/../$1"{,.exe}; do
        if [[ -x $candidate ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done
    return 0
}
codec=${SPRA_CODEC:-$(find_tool reduced-codec)}
bpe=${SPRA_BPE:-$(find_tool spra-bpe)}
if [[ -z $bpe || ( -z $codec && ( $operation == encode || $operation == decode ) ) ]]; then
    printf '%s\n' 'spra-bpe-xz: tools not found; build tools/reduced_codec.cpp and tools/bpe_layer.cpp (docs/bpe_layer.md), or set SPRA_CODEC and SPRA_BPE.' >&2
    exit 1
fi

input=${tool_args[0]:--}
decode_args=(); limits=(); input=-; has_input=false
if [[ $operation != encode ]]; then
    for ((i=0; i<${#tool_args[@]}; ++i)); do
        arg=${tool_args[i]}
        if [[ $arg == --max-symbols && ( $operation == decode || $operation == unpack ) ]]; then
            ((i+1 < ${#tool_args[@]})) || { usage >&2; exit 1; }
            limits+=("$arg" "${tool_args[i+1]}"); i=$((i+1))
        elif [[ $arg == --max-symbols=* && ( $operation == decode || $operation == unpack ) ]]; then limits+=("$arg")
        elif [[ $arg == --strict && $operation == decode ]]; then decode_args+=("$arg")
        elif ! $has_input && [[ $arg == - || $arg != -* ]]; then input=$arg; has_input=true
        else usage >&2; exit 1; fi
    done
fi
case $operation in
    encode) "$codec" encode "${tool_args[@]}" | "$bpe" encode "${bpe_args[@]}" | xz -c "${pack_args[@]}";;
    decode) xz -dc ${xz_args[@]+"${xz_args[@]}"} -- "$input" | "$bpe" decode "${limits[@]}" | "$codec" decode "${decode_args[@]}" "${limits[@]}";;
    pack) xz -dc -- "$input" | "$bpe" encode "${bpe_args[@]}" | xz -c "${pack_args[@]}";;
    unpack) xz -dc ${xz_args[@]+"${xz_args[@]}"} -- "$input" | "$bpe" decode "${limits[@]}";;
esac
