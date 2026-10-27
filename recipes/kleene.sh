#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tool=${KLEENE_TOOL:-"$root/build/recipes/kleene"}
usage() {
    echo 'usage: bash recipes/kleene.sh [LIMITS] compile|emit|info|audit [FILE|-]
compile: SPRA 0.2 -> Kleene .kl source with SPRA comment metadata
emit:    Kleene source -> binary SPRA 0.2 on stdout
info:    recipe metadata as JSON
audit:   derive tuple/packing statistics without expanding the recipe
FILE ending in .xz is decompressed first; omitted FILE reads stdin.
Requires make -C recipes. KLEENE_TOOL overrides the executable.
The .kl runner supports a finite singleton-expression profile of Kleene.
All recipes use one-based labels, the identity seed and canonical SPRA packing.
Expansion streams SPRA bytes; source is checked before output begins.
LIMITS (positive decimal counts, before or after the command/file):
  --max-nodes COUNT         expression nodes and definitions (default 2000000)
  --max-edges COUNT         child references (default 32000000)
  --max-source-bytes COUNT  decompressed source bytes (default 268435456)
  --max-nesting COUNT       expression/comment nesting (default 256)
Overrides change resource budgets, not output length or tuple semantics.'
}
limits=() positional=()
while (($#)); do
    case $1 in
        --help) usage; exit 0;;
        --max-nodes|--max-edges|--max-source-bytes|--max-nesting)
            if (($#<2)); then echo "Missing count for $1" >&2; exit 1; fi
            limits+=("$1" "$2"); shift 2;;
        --max-nodes=*|--max-edges=*|--max-source-bytes=*|--max-nesting=*)
            limits+=("$1"); shift;;
        --) shift; positional+=("$@"); break;;
        --*) echo "Unknown recipe option: $1" >&2; exit 1;;
        *) positional+=("$1"); shift;;
    esac
done
if ((${#positional[@]}<1 || ${#positional[@]}>2)); then usage >&2; exit 1; fi
operation=${positional[0]} input=${positional[1]:--}
case $operation in
    compile|emit|info|audit) ;;
    *) echo 'Unknown recipe command' >&2; exit 1;;
esac
if [[ $input == *.xz ]]; then
    if [[ $operation == compile ]]; then xz -dc -- "$input" | "$tool" "${limits[@]}" "$operation"
    else xz -dc -- "$input" | "$tool" "${limits[@]}" "$operation" -; fi
elif [[ $operation == compile ]]; then
    if [[ $input == - ]]; then "$tool" "${limits[@]}" "$operation"; else "$tool" "${limits[@]}" "$operation" < "$input"; fi
else "$tool" "${limits[@]}" "$operation" -- "$input"; fi
