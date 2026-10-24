#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jakub Halfar
set -euo pipefail

usage() {
    printf '%s\n' 'usage: spra-bpe-xz.sh encode [--bits 4|6] [-z] N [INPUT|-] [-- XZ_ARGS...]
       spra-bpe-xz.sh decode [INPUT|-] [-- XZ_ARGS...]
       spra-bpe-xz.sh pack [INPUT|-] [-- XZ_ARGS...]
       spra-bpe-xz.sh unpack [INPUT|-] [-- XZ_ARGS...]

The optional SPBE layer (docs/bpe_layer.md) between SPRA 0.2 and xz.
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
while (($#)) && [[ $1 != -- ]]; do
    tool_args+=("$1")
    shift
done
if (($#)); then shift; fi
xz_args=("$@")
if [[ $operation != encode && ${#tool_args[@]} -gt 1 ]]; then
    usage >&2
    exit 1
fi
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
}
codec=${SPRA_CODEC:-$(find_tool reduced-codec)}
bpe=${SPRA_BPE:-$(find_tool spra-bpe)}
if [[ -z $bpe || ( -z $codec && ( $operation == encode || $operation == decode ) ) ]]; then
    printf '%s\n' 'spra-bpe-xz: tools not found; build tools/reduced_codec.cpp and tools/bpe_layer.cpp (docs/bpe_layer.md), or set SPRA_CODEC and SPRA_BPE.' >&2
    exit 1
fi

input=${tool_args[0]:--}
case $operation in
    encode) "$codec" encode "${tool_args[@]}" | "$bpe" encode | xz -c "${pack_args[@]}";;
    decode) xz -dc ${xz_args[@]+"${xz_args[@]}"} -- "$input" | "$bpe" decode | "$codec" decode;;
    pack) xz -dc -- "$input" | "$bpe" encode | xz -c "${pack_args[@]}";;
    unpack) xz -dc ${xz_args[@]+"${xz_args[@]}"} -- "$input" | "$bpe" decode;;
esac
