#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
tool=${KLEENE_TOOL:-"$root/build/recipes/kleene"}
codec=${SPRA_CODEC:-"$root/build/reduced_codec"}
wrapper=$root/recipes/kleene.sh
export KLEENE_TOOL=$tool
cat > "$work/limits.kl" <<'KL'
// @spra version 1
// @spra n 3
// @spra length 5
$atom = '[0,+2]';
$word = $atom{3};
KL
printf 12222 | "$codec" encode 3 > "$work/expected"
expect_limit() {
    local option=$1; shift
    if bash "$wrapper" "$@" > "$work/out" 2> "$work/error"; then
        echo "Limit was not enforced: $option" >&2; exit 1
    fi
    [[ ! -s $work/out ]]
    grep -q -- "$option" "$work/error"
}
# A repeat adds a second node. Named aliases consume the same configured
# definition budget even if interning shares their expression nodes.
expect_limit --max-nodes emit "$work/limits.kl" --max-nodes 1
bash "$wrapper" --max-nodes 2 emit "$work/limits.kl" | cmp - "$work/expected"
head -n 3 "$work/limits.kl" > "$work/aliases.kl"
printf '%s\n' '$a = _e_; $b = $a; $word = $b;' >> "$work/aliases.kl"
expect_limit --max-nodes info "$work/aliases.kl" --max-nodes=2
bash "$wrapper" info --max-nodes=3 "$work/aliases.kl" | jq -e '.logical_tuples==0' >/dev/null
# Independent canonical three-tuple control exercises reference accounting.
head -n 3 "$work/limits.kl" > "$work/edges.kl"
printf '%s\n' "\$word = '[0,+2]' '[0,0]' '[0,+1]';" >> "$work/edges.kl"
printf 12212 | "$codec" encode 3 > "$work/edges.spra"
expect_limit --max-edges emit "$work/edges.kl" --max-edges 2
bash "$wrapper" emit "$work/edges.kl" --max-edges=3 | cmp - "$work/edges.spra"
# Exact decompressed-byte boundary, including compressed input and stdin.
bytes=$(wc -c < "$work/limits.kl")
expect_limit --max-source-bytes info "$work/limits.kl" --max-source-bytes "$((bytes-1))"
xz -c "$work/limits.kl" > "$work/a recipe.kl.xz"
bash "$wrapper" emit "$work/a recipe.kl.xz" --max-source-bytes "$bytes" --max-nodes=2 | cmp - "$work/expected"
cat "$work/limits.kl" | bash "$wrapper" --max-nodes=2 emit | cmp - "$work/expected"
# Each nesting limit can be raised without changing source syntax or output.
head -n 3 "$work/limits.kl" > "$work/nested.kl"
printf '%s\n' "\$word = ((('[0,+2]'))){3};" >> "$work/nested.kl"
expect_limit --max-nesting emit "$work/nested.kl" --max-nesting 2
bash "$wrapper" --max-nesting 3 emit "$work/nested.kl" | cmp - "$work/expected"
head -n 3 "$work/limits.kl" > "$work/comments.kl"
printf '%s\n' '/* outer /* middle /* inner */ middle */ outer */' "\$word = '[0,+2]'{3};" >> "$work/comments.kl"
expect_limit --max-nesting emit "$work/comments.kl" --max-nesting 2
bash "$wrapper" emit "$work/comments.kl" --max-nesting 3 | cmp - "$work/expected"
# The compiler receives budgets too, not just the source parser.
expect_limit --max-nodes compile "$work/edges.spra" --max-nodes 1
bash "$wrapper" compile "$work/edges.spra" --max-nodes 20 > "$work/compiled.kl"
bash "$wrapper" emit "$work/compiled.kl" | cmp - "$work/edges.spra"
# Direct executable and wrapper accept both option positions/forms. Invalid
# numbers and unknown options fail before any SPRA output.
"$tool" --max-nodes 2 emit "$work/limits.kl" --max-edges=1 | cmp - "$work/expected"
for value in 0 -1 +1 1x '' 18446744073709551616 4294967296; do
    expect_limit --max-nodes emit "$work/limits.kl" --max-nodes "$value"
done
expect_limit --max-nodes emit "$work/limits.kl" --max-nodes
expect_limit --max-unknown emit "$work/limits.kl" --max-unknown 1
for option in --max-edges --max-source-bytes --max-nesting; do
    expect_limit "$option" emit "$work/limits.kl" "$option" 0
done
# Raised budgets do not bypass tuple or count-overflow validity checks.
head -n 3 "$work/limits.kl" > "$work/invalid.kl"
printf '%s\n' "\$word = '[1,0]';" >> "$work/invalid.kl"
if bash "$wrapper" emit "$work/invalid.kl" --max-nodes 3000000 > "$work/out" 2>/dev/null; then exit 1; fi
[[ ! -s $work/out ]]
head -n 3 "$work/limits.kl" > "$work/overflow.kl"
printf '%s\n' "\$word = '[0,+2]'{9223372036854775807}{3};" >> "$work/overflow.kl"
if bash "$wrapper" emit "$work/overflow.kl" --max-nodes 3000000 > "$work/out" 2>/dev/null; then exit 1; fi
[[ ! -s $work/out ]]
echo 'Kleene resource limits: node/definition, reference, source-byte and nesting overrides passed'
