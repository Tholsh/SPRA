#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
    printf '%s\n' 'usage: bash tools/decompress-folder.sh SOURCE_DIRECTORY [OUTPUT_DIRECTORY]

Recursively decompress word files, preserving the source directory structure.
Supports .spra.xz, .bpe.xz, .spbe.xz, bare .spra/.spbe, .txt.xz, and .tar.xz.
Without OUTPUT_DIRECTORY, text files are written beside their source archives.
Existing text files are skipped in this mode; archives are preserved.
An explicit output may be the source itself or a new directory outside it.
For separate output, existing .txt files are copied. Other files are skipped.
Requires Bash, xz, tar and the codec tools used by the SPRA/BPE wrappers.
SPRA_CODEC and SPRA_BPE override the decoder executables.
Expanded files can be very large. On failure, completed output files remain.'
}
if [[ ${1:-} == --help || ${1:-} == -h ]]; then usage; exit 0; fi
if (($# < 1 || $# > 2)); then usage >&2; exit 1; fi
source_dir=$(cd -- "$1" && pwd -P)
script_path=${BASH_SOURCE[0]//\\//}
script_dir=$(cd -- "$(dirname -- "$script_path")" && pwd -P)
output_dir=$source_dir
in_place=true
if (($# == 2)); then
    if [[ -d $2 && $(cd -- "$2" && pwd -P) == "$source_dir" ]]; then
        output_dir=$source_dir
    else
        if [[ -e $2 || -L $2 ]]; then echo "decompress-folder: output already exists: $2" >&2; exit 1; fi
        mkdir -p -- "$(dirname -- "$2")"
        parent=$(cd -- "$(dirname -- "$2")" && pwd -P)
        output_dir="$parent/$(basename -- "$2")"
        if [[ $output_dir == "$source_dir/"* ]]; then
            echo 'decompress-folder: separate output must be outside the source directory' >&2; exit 1
        fi
        mkdir -- "$output_dir"
        in_place=false
    fi
fi
find_tool() {
    local name candidate
    for name in "$@"; do
        for candidate in "$script_dir/../build/$name" "$script_dir/../$name"; do
            if [[ -x $candidate ]]; then printf '%s\n' "$candidate"; return; fi
        done
    done
    echo "decompress-folder: build $1 or set its executable override" >&2
    return 1
}

find "$source_dir" -type f \( -name '*.spra.xz' -o -name '*.bpe.xz' -o -name '*.spbe.xz' \
    -o -name '*.spra' -o -name '*.spbe' -o -name '*.txt.xz' -o -name '*.tar.xz' -o -name '*.txt' \) -print0 |
while IFS= read -r -d '' source; do
    relative=${source#"$source_dir/"}
    if [[ $relative == *.tar.xz ]]; then
        target="$output_dir/$(dirname -- "$relative")"
        mkdir -p -- "$target"
        tar --skip-old-files -xJf "$source" -C "$target"
        printf 'Extracted %s\n' "$relative" >&2
        continue
    fi
    case $relative in
        *.spra.xz) relative=${relative%.spra.xz}.txt; kind=spra_xz;;
        *.bpe.xz) relative=${relative%.bpe.xz}.txt; kind=bpe_xz;;
        *.spbe.xz) relative=${relative%.spbe.xz}.txt; kind=bpe_xz;;
        *.spra) relative=${relative%.spra}.txt; kind=spra;;
        *.spbe) relative=${relative%.spbe}.txt; kind=spbe;;
        *.txt.xz) relative=${relative%.xz}; kind=text_xz;;
        *.txt) if $in_place; then continue; fi; kind=text;;
    esac
    target="$output_dir/$relative"
    if [[ -e $target || -L $target ]]; then
        if $in_place; then printf 'Skipped existing %s\n' "$relative" >&2; continue; fi
        echo "decompress-folder: output collision: $relative" >&2; exit 1
    fi
    mkdir -p -- "$(dirname -- "$target")"
    partial=$(mktemp "$(dirname -- "$target")/.spra-decode.XXXXXXXX")
    trap 'rm -f -- "$partial"' EXIT
    if case $kind in
        spra_xz) bash "$script_dir/spra-xz.sh" decode "$source" > "$partial";;
        bpe_xz) bash "$script_dir/spra-bpe-xz.sh" decode "$source" > "$partial";;
        spra)
            codec=${SPRA_CODEC:-$(find_tool reduced_codec reduced-codec)}
            "$codec" decode "$source" > "$partial";;
        spbe)
            codec=${SPRA_CODEC:-$(find_tool reduced_codec reduced-codec)}
            bpe=${SPRA_BPE:-$(find_tool spra-bpe)}
            "$bpe" decode "$source" | "$codec" decode > "$partial";;
        text_xz) xz -dc -- "$source" > "$partial";;
        text) cat -- "$source" > "$partial";;
    esac; then
        mv -- "$partial" "$target"
    else
        rm -f -- "$partial"
        trap - EXIT
        exit 1
    fi
    trap - EXIT
    printf 'Wrote %s\n' "$relative" >&2
done
printf 'Decompressed to %s\n' "$output_dir" >&2
