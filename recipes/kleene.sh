#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tool=${KLEENE_TOOL:-"$root/build/recipes/kleene"}
if [[ ${1:-} == --help || $# -lt 1 || $# -gt 2 ]]; then
    echo 'usage: bash recipes/kleene.sh compile|emit|info|audit [FILE|-]
compile: SPRA 0.2 -> Kleene .kl source with SPRA comment metadata
emit:    Kleene source -> binary SPRA 0.2 on stdout
info:    recipe metadata as JSON
audit:   derive tuple/packing statistics without expanding the recipe
FILE ending in .xz is decompressed first; omitted FILE reads stdin.
Requires make -C recipes. KLEENE_TOOL overrides the executable.
The .kl runner supports a finite singleton-expression profile of Kleene.
All recipes use one-based labels, the identity seed and canonical SPRA packing.
Expansion streams SPRA bytes; source is checked before output begins.'
    [[ ${1:-} == --help ]]
    exit
fi
operation=$1 input=${2:--}
case $operation in
    compile|emit|info|audit) ;;
    *) echo 'Unknown recipe command' >&2; exit 1;;
esac
if [[ $input == *.xz ]]; then
    if [[ $operation == compile ]]; then xz -dc -- "$input" | "$tool" "$operation"
    else xz -dc -- "$input" | "$tool" "$operation" -; fi
elif [[ $operation == compile ]]; then
    if [[ $input == - ]]; then "$tool" "$operation"; else "$tool" "$operation" < "$input"; fi
else "$tool" "$operation" "$input"; fi
