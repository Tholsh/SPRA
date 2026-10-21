#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
set -euo pipefail

usage() {
    printf '%s\n' 'usage: spra-xz.sh encode [--bits 4|6] [-z] N [INPUT|-] [-- XZ_ARGS...]
       spra-xz.sh decode [INPUT|-] [-- XZ_ARGS...]

Encode a word to SPRA 0.2 plus xz, or decode a .spra.xz archive.
Both commands write only their result to stdout and preserve input files.
Omitted INPUT or '-' reads stdin. Pass xz options after '--', e.g. -- -6e.
SPRA_CODEC overrides the codec executable; otherwise build/ and the repository
root are searched. Build tools/reduced_codec.cpp first; see docs/corpus.md.'
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
if [[ ${1:-} != encode && ${1:-} != decode ]]; then
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
if [[ $operation == decode && ${#codec_args[@]} -gt 1 ]]; then
    usage >&2
    exit 1
fi

script_path=${BASH_SOURCE[0]//\\//}
if [[ $script_path == */* ]]; then
    script_dir=$(cd -- "${script_path%/*}" && pwd)
else
    script_dir=$PWD
fi
codec=${SPRA_CODEC:-}
if [[ -z $codec ]]; then
    for candidate in "$script_dir/../build/reduced-codec"{,.exe} \
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

if [[ $operation == encode ]]; then
    "$codec" encode "${codec_args[@]}" | xz -c "${xz_args[@]}"
else
    xz -dc "${xz_args[@]}" -- "${codec_args[0]:--}" | "$codec" decode
fi
