#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
set -euo pipefail

usage() {
    printf '%s\n' 'usage: bash docs/unpack-corpus.sh [all|N] [OUTPUT_DIRECTORY]

Materialize one alphabet or the entire corpus as .txt files.
Defaults: all alphabets, REPOSITORY/build/unpacked.
The output directory must not exist. Archives are preserved.
N=6 collection tar archives are extracted; SPRA uses tools/spra-xz.sh.
Existing text words are copied, and older .txt.xz words are decompressed.
Source labels and bytes are retained; SPRA output has no whitespace.
Requires Bash, tar, and xz. See docs/corpus.md for streaming without files.'
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
if (($# > 2)); then usage >&2; exit 1; fi
selection=${1:-all}
if [[ $selection != all && ! $selection =~ ^([1-9]|[12][0-9]|3[0-2])$ ]]; then
    usage >&2
    exit 1
fi
script_path=${BASH_SOURCE[0]//\\//}
repo=$(cd -- "${script_path%/*}/.." && pwd -P)
output=${2:-"$repo/build/unpacked"}
if [[ -e $output || -L $output ]]; then
    printf 'unpack-corpus: output already exists: %s\n' "$output" >&2
    exit 1
fi
mkdir -p -- "$(dirname -- "$output")"
parent=$(cd -- "$(dirname -- "$output")" && pwd -P)
output="$parent/$(basename -- "$output")"
if [[ $output == "$repo/words" || $output == "$repo/words/"* ]]; then
    printf '%s\n' 'unpack-corpus: choose an output outside the source words/ tree' >&2
    exit 1
fi
staging=$(mktemp -d "$parent/.spra-unpack.XXXXXXXX")
cleanup() {
    # mktemp created this absolute directory directly under the resolved parent.
    if [[ -n $staging && $staging == "$parent"/.spra-unpack.* && -d $staging ]]; then
        rm -rf -- "$staging"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

unpack_n() {
    local n=$1 source relative target
    if [[ -d $repo/words/$n ]]; then
        printf 'Materializing N=%s words\n' "$n" >&2
        find "$repo/words/$n" -type f \( -name '*.tar.xz' -o -name '*.spra.xz' -o -name '*.txt.xz' -o -name '*.txt' \) -print0 |
            while IFS= read -r -d '' source; do
                relative=${source#"$repo/words/"}
                case $relative in
                    *.tar.xz)
                        target="$staging/$(dirname -- "$relative")"
                        mkdir -p -- "$target"
                        tar -xJf "$source" -C "$target"
                        ;;
                    *.spra.xz)
                        target="$staging/${relative%.spra.xz}.txt"
                        mkdir -p -- "$(dirname -- "$target")"
                        bash "$repo/tools/spra-xz.sh" decode "$source" > "$target"
                        ;;
                    *.txt.xz)
                        target="$staging/${relative%.xz}"
                        mkdir -p -- "$(dirname -- "$target")"
                        xz -dc -- "$source" > "$target"
                        ;;
                    *.txt)
                        target="$staging/$relative"
                        mkdir -p -- "$(dirname -- "$target")"
                        cp -- "$source" "$target"
                        ;;
                esac
            done
    else
        printf 'unpack-corpus: no corpus for N=%s\n' "$n" >&2
        return 1
    fi
}

if [[ $selection == all ]]; then
    found=false
    for n in {1..32}; do
        if [[ -d $repo/words/$n ]]; then
            unpack_n "$n"
            found=true
        fi
    done
    if [[ $found != true ]]; then
        printf '%s\n' 'unpack-corpus: no corpus archives found' >&2
        exit 1
    fi
else
    unpack_n "$selection"
fi
if [[ -e $output || -L $output ]]; then
    printf 'unpack-corpus: output appeared during extraction: %s\n' "$output" >&2
    exit 1
fi
mv -- "$staging" "$output"
staging=''
printf 'Unpacked to %s\n' "$output" >&2
