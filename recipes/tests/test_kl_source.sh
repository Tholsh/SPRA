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
cat > "$work/hand.kl" <<'KL'
// Metadata is a wrapper convention in ordinary Kleene comments.
// @spra version 1
// @spra n 3
// @spra length 5
/* Kleene permits /* nested */ comments. */
$atom = '[0,+2]';
$empty = _e_{9223372036854775807};
$word = $empty ($atom{2} $atom) _e_;
KL
printf 12222 > "$work/word"
"$codec" encode 3 "$work/word" > "$work/expected"
bash "$wrapper" emit "$work/hand.kl" > "$work/out"
cmp "$work/expected" "$work/out"
"$codec" decode --strict "$work/out" | cmp - "$work/word"
bash "$wrapper" info "$work/hand.kl" | jq -e '.n==3 and .bits==4 and .symbols==5 and .logical_tuples==3' >/dev/null
bash "$wrapper" audit "$work/hand.kl" | jq -e '.physical_tokens==2 and .runs==1' >/dev/null
# Filename is a convenience: comment metadata continues to work after rename.
xz -c "$work/hand.kl" > "$work/a renamed recipe.kl.xz"
bash "$wrapper" emit "$work/a renamed recipe.kl.xz" | cmp - "$work/expected"
cat "$work/hand.kl" | bash "$wrapper" emit | cmp - "$work/expected"
# Canonical identity-seeded words, empty streams and seed-only words.
for word in '' 1 12222 123121321 123123; do
    printf %s "$word" > "$work/word"
    "$codec" encode 3 "$work/word" > "$work/expected"
    bash "$wrapper" compile "$work/expected" > "$work/mined.kl"
    grep -q '^// @spra version 1$' "$work/mined.kl"
    grep -q '^\$word = ' "$work/mined.kl"
    bash "$wrapper" emit "$work/mined.kl" > "$work/out"
    cmp "$work/expected" "$work/out"
    "$codec" decode --strict "$work/out" | cmp - "$work/word"
done
# Independent corpus controls at different N. Importing forced six-bit input
# must produce the default canonical width rather than preserve a wider layout.
for n in 5 7 8; do
    case $n in
        5) source=$root/words/5/153/153-recursive.txt;;
        7) source=$root/words/7/5913/5913-palindromic.txt;;
        8) source=$root/words/8/46181/superperm8_46181_1.txt;;
    esac
    "$codec" encode "$n" "$source" > "$work/expected"
    "$codec" encode --bits 6 "$n" "$source" > "$work/wider.spra"
    xz -c "$work/wider.spra" > "$work/wider.spra.xz"
    bash "$wrapper" compile "$work/wider.spra.xz" > "$work/mined.kl"
    bash "$wrapper" emit "$work/mined.kl" > "$work/out"
    cmp "$work/expected" "$work/out"
    "$codec" decode --strict "$work/out" | "$root/build/paint-waste" -s "$n" > "$work/stats"
    grep -qx 'valid_superpermutation: 1' "$work/stats"
    bash "$wrapper" compile "$work/wider.spra" > "$work/second.kl"
    cmp "$work/mined.kl" "$work/second.kl"
done
# Header derivation for billions of tuple actions is algebraic; no expansion.
cat > "$work/large.kl" <<'KL'
// @spra version 1
// @spra n 14
// @spra length 93904804425
$word = '[0,+13]'{6706924412};
KL
bash "$wrapper" info "$work/large.kl" | jq -e '.bits==6 and .logical_tuples==6706924412' >/dev/null
# The synthetic machine above is only a metadata/count control, not a claim
# of valid coverage. All structural failures must precede binary output.
expect_bad() {
    if bash "$wrapper" emit "$work/bad.kl" > "$work/bad-out" 2>/dev/null; then
        echo 'Malformed or unsupported Kleene recipe accepted' >&2; exit 1
    fi
    [[ ! -s $work/bad-out ]]
}
head -n 4 "$work/hand.kl" > "$work/header"
for body in \
    "\$word = '[0,+2]'*;" \
    "\$word = '[0,+2]' | '[0,+1]';" \
    "\$word = \$later; \$later = '[0,+2]';" \
    "\$word = \$word;" \
    "\$a = '[0,+2]'; \$a = '[0,+1]'; \$word = \$a;" \
    "\$word = '[0,+2]'{18446744073709551615}{2};" \
    "\$word = '[0,+2]'{4};" \
    "\$word = '[1,0]';" \
    "\$word = '[0,+3]';" \
    "\$word = '[64,+1]';" \
    "\$word = '[0,-33]';" \
    "\$word = abc;" \
    "\$word = '[0,+2]'; // @spra n 3" \
    "\$word = '[0,+2]'" \
    "\$word = '[0,+2]'; /* unterminated"; do
    cat "$work/header" > "$work/bad.kl"
    printf '%s\n' "$body" >> "$work/bad.kl"
    expect_bad
done
for replacement in \
    's/@spra version 1/@spra version 2/' \
    's/@spra n 3/@spra n 33/' \
    's/@spra length 5/@spra length 18446744073709551616/' \
    's/@spra n 3/@spra labels one-based/' \
    's/@spra n 3/@spra seed 0 1/' \
    '/@spra n /d'; do
    sed "$replacement" "$work/hand.kl" > "$work/bad.kl"
    expect_bad
done
cat "$work/header" "$work/hand.kl" > "$work/bad.kl"
expect_bad
# Valid non-nearest copies are accepted by the streamer. The independent
# strict decoder remains responsible for nearest-copy conformance.
cat "$work/header" > "$work/nonnearest.kl"
printf '%s\n' "\$word = '[0,+2]'{2} '[0,+1]';" >> "$work/nonnearest.kl"
bash "$wrapper" emit "$work/nonnearest.kl" > "$work/nonnearest.spra"
"$codec" decode "$work/nonnearest.spra" > /dev/null
if "$codec" decode --strict "$work/nonnearest.spra" > /dev/null 2>&1; then exit 1; fi
# A clean step from a repeated suffix and an unavailable reference are invalid.
for body in \
    "\$word = '[0,+2]' '[1,+2]';" \
    "\$word = '[0,-1]';"; do
    cat "$work/header" > "$work/bad.kl"
    printf '%s\n' "$body" >> "$work/bad.kl"
    expect_bad
done
# Fixed seed/labels normalize a bijective source gauge. A repeated source seed
# cannot be normalized by relabeling and must be refused before writing text.
printf 123121321 > "$work/canonical-word"
"$codec" encode 3 "$work/canonical-word" > "$work/expected"
for word in 231232132 012010210; do
    printf %s "$word" > "$work/word"
    flags=()
    if [[ $word == 012010210 ]]; then flags=(-z); fi
    "$codec" encode "${flags[@]}" 3 "$work/word" > "$work/source.spra"
    bash "$wrapper" compile "$work/source.spra" > "$work/normalized.kl"
    bash "$wrapper" emit "$work/normalized.kl" | cmp - "$work/expected"
done
printf 11111 > "$work/word"
"$codec" encode 3 "$work/word" > "$work/source.spra"
if bash "$wrapper" compile "$work/source.spra" > "$work/bad-out" 2>/dev/null; then exit 1; fi
[[ ! -s $work/bad-out ]]
# No legacy-format reader or command aliases are retained.
printf 'KLEENE-SPRA 1\n' > "$work/bad.kl"
expect_bad
if bash "$wrapper" compile-legacy > /dev/null 2>&1; then exit 1; fi
# An xz error must propagate through the public streaming command.
head -c -8 "$work/a renamed recipe.kl.xz" > "$work/broken.xz"
if bash "$wrapper" emit "$work/broken.xz" > /dev/null 2>/dev/null; then exit 1; fi
echo 'Kleene .kl: metadata, tuple atoms, lazy powers, canonical corpus streams, gauge normalization and refusal checks passed'
