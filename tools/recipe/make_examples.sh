#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jakub Halfar
# Rebuild the example recipe containers words/N/LENGTH/superpermutation-N-LENGTH.sprc from the published files.
#
# usage: bash tools/recipe/make_examples.sh SUPERPERMUTATIONS PANTONE [OUT_ROOT]
#   SUPERPERMUTATIONS  a checkout of github.com/jhalfar/superpermutations (commit 771f9dc or later)
#   PANTONE            a checkout of github.com/jaypantone/superperm-upper-43-80 (tools/construction-input.txt)
#   OUT_ROOT           where words/ is written (default: this repository)
# PYTHON selects the interpreter (default python3). Packing takes a few seconds; it does not decode.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../.." && pwd)
pub=${1:?usage: make_examples.sh SUPERPERMUTATIONS PANTONE [OUT_ROOT]}
pant=${2:?usage: make_examples.sh SUPERPERMUTATIONS PANTONE [OUT_ROOT]}
out=${3:-$root}
py=${PYTHON:-python3}
rc() { "$py" -E -s "$here/recipe.py" "$@"; }
w=$(mktemp -d "${TMPDIR:-/tmp}/sprc-make.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT

# Pantone's 10-symbol input (LF line ends), his 9- and 8-symbol selections, their transports
tr -d '\r' < "$pant/tools/construction-input.txt" > "$w/in_lf.txt"
"$py" -E -s "$here/gen/pantone_py/untransport.py" "$w/in_lf.txt" --out "$w/sel9.txt" > /dev/null
"$py" -E -s "$here/gen/pantone_py/untransport.py" "$w/sel9.txt" --out "$w/base8.txt" > /dev/null
rc selpack "$w/base8.txt" "$w/base8.sel"
rc transport "$w/base8.txt" 1 "$w/t9.txt"
rc transport "$w/base8.txt" 2 "$w/t10.txt"

# selection data
rc seldiff "$w/t9.txt" "$pub/reproduce/selections/n10-selection.txt" "$w/n10.diff"
rc seldiff --rows-only "$w/t10.txt" "$w/in_lf.txt" "$w/in56.diff"
rc selpack "$pub/selection/data/block_7_2_n11.txt" "$w/b72.blk" 7 2
rc selpack "$pub/selection/data/block_7_3_n12.txt" "$w/b73.blk" 7 3
rc selpack "$pub/selection/data/block_7_3_c.txt" "$w/b73c.blk" 7 3
rc selpack "$pub/selection/data/block_7_4_n13.txt" "$w/b74.blk" 7 4
cp "$pub/selection/data/connect_n11_a.json" "$w/connect_a.json"
cp "$pub/selection/data/connect_n11_b.json" "$w/connect_b.json"

# plans
rc planenc "$pub/reproduce/plan/n10-4034855.plan" "$w/plan10.plc"
rc planenc "$pub/reproduce/plan/n11-43930578.plan" "$w/plan11.plc"
rc planenc "$pub/plan/trailsearch-12-522736969.plan" "$w/plan12.plc"
rc planenc "$pub/plan/trailsearch-13-6747801835.plan" "$w/plan13.plc"
xz -dc "$pub/recipes/n14/n14_93904804425_on_selC.uplan.xz" > "$w/plan14.txt"
rc planenc "$w/plan14.txt" "$w/plan14.plc"

# n alphabet length sha256-of-the-letters, then the parts
make() {
    local n=$1 len=$2 sha=$3
    shift 3
    mkdir -p "$out/words/$n/$len"
    local parts=()
    for p in "$@"; do parts+=(--part "$p"); done
    rc pack "$out/words/$n/$len/superpermutation-$n-$len.sprc" --n "$n" \
        --alphabet "$(printf '%s' 0123456789ABCD | head -c "$n")" --length "$len" --sha256 "$sha" \
        --steps "$here/examples/n$n.steps" "${parts[@]}"
}
make 10 4034855 542a3778662f972a4ea7b9461a9334775f31303a0d8b1a7913169d8beea444ff \
    base8.sel="$w/base8.sel" n10.diff="$w/n10.diff" plan.plc="$w/plan10.plc"
make 11 43930578 f7d3122f350d56d41945961f9abf36453420bd3068a3941b424f9c0447c7aef5 \
    base8.sel="$w/base8.sel" in56.diff="$w/in56.diff" b72.blk="$w/b72.blk" \
    connect_a.json="$w/connect_a.json" connect_b.json="$w/connect_b.json" plan.plc="$w/plan11.plc"
make 12 522736969 eb4861f06f9eaa652bf355ff5e3eac2a3080c1e7dee72f527e517c949b2d6918 \
    base8.sel="$w/base8.sel" n10.diff="$w/n10.diff" b73.blk="$w/b73.blk" plan.plc="$w/plan12.plc"
make 13 6747801835 8905fc686861187923accc190c67773880fcee3aa669ba63c9ca80f778879387 \
    base8.sel="$w/base8.sel" n10.diff="$w/n10.diff" b73c.blk="$w/b73c.blk" b74.blk="$w/b74.blk" \
    plan.plc="$w/plan13.plc"
make 14 93904804425 3b1470a204854ce09654b9d9a811b8455308212b29585accc29d0263e423ab6f \
    base8.sel="$w/base8.sel" n10.diff="$w/n10.diff" b73c.blk="$w/b73c.blk" plan.plc="$w/plan14.plc"
