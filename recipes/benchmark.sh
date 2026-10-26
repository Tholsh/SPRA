#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
unset XZ_OPT XZ_DEFAULTS
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
script=$root/recipes/benchmark.sh
tool=${KLEENE_TOOL:-"$root/build/recipes/kleene"}
wrapper=$root/recipes/kleene.sh
if [[ ${1:-} == --stage ]]; then
    shift
    case $1 in
        compile) xz -dc -- "$2" | "$tool" compile > "$3";;
        emit) bash "$wrapper" emit "$2" > /dev/null;;
        baseline) xz -dc -- "$2" > /dev/null;;
        paint-recipe) bash "$wrapper" emit "$2" | "$root/build/reduced_codec" decode --strict | "$root/build/paint-waste" -s "$3";;
        paint-source) bash "$root/tools/spra-xz.sh" decode --strict "$2" | "$root/build/paint-waste" -s "$3";;
        *) exit 1;;
    esac
    exit
fi
if (($#<3 || $#>4)) || [[ ${4:-} != '' && ${4:-} != --paint ]]; then
    echo 'usage: bash recipes/benchmark.sh N SOURCE.spra.xz NEW_OUTPUT_DIR [--paint]
Benchmarks tuple mining, recipe packing/loading/audit, streaming expansion,
and baseline xz decompression. Expanded SPRA goes to /dev/null.
--paint compares complete paint-waste -s reports byte for byte (N<=14).
No expanded files or hashes are produced. GNU time, jq, xz are required.' >&2
    exit 1
fi
n=$1 source=$(realpath -- "$2") output=$3
[[ $n =~ ^([1-9]|[12][0-9]|3[0-2])$ ]] || { echo 'Invalid N' >&2; exit 1; }
[[ $source == *.spra.xz && -f $source ]] || { echo 'Source must be a .spra.xz archive' >&2; exit 1; }
if [[ ${4:-} == --paint ]] && ((n>14)); then
    echo 'paint-waste currently supports only N<=14' >&2; exit 1
fi
[[ ! -e $output ]] || { echo 'Output directory must be new' >&2; exit 1; }
mkdir -p -- "$output"
output=$(realpath -- "$output")
measure() {
    local label=$1; shift
    /usr/bin/time -f '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"peak_process_rss_kib":%M,"exit_code":%x}' \
        -o "$output/$label.time.json" "$@"
}
measure compile bash "$script" --stage compile "$source" "$output/word.kl" 2> "$output/compile.log"
measure compress xz -c -T1 -6 "$output/word.kl" > "$output/word.kl.xz"
measure audit bash "$wrapper" audit "$output/word.kl.xz" > "$output/audit.json"
jq -e --argjson n "$n" '.n==$n' "$output/audit.json" > /dev/null
measure emit bash "$script" --stage emit "$output/word.kl.xz" 2> "$output/emit.log"
measure baseline bash "$script" --stage baseline "$source"
# Check actual emitted counters against the compositional audit, without
# hashing or materializing the expanded SPRA or raw symbols.
jq -e -s '.[0].spra_bytes==.[1].spra_bytes and .[0].physical_tokens==.[1].physical_tokens and .[0].logical_tuples==.[1].logical_tuples' \
    "$output/audit.json" "$output/emit.log" >/dev/null
validation=recipe_audit_and_emission_counts
if [[ ${4:-} == --paint ]]; then
    measure paint_recipe bash "$script" --stage paint-recipe "$output/word.kl.xz" "$n" > "$output/recipe.stats" 2> "$output/paint-recipe.log"
    measure paint_source bash "$script" --stage paint-source "$source" "$n" > "$output/source.stats" 2> "$output/paint-source.log"
    cmp "$output/recipe.stats" "$output/source.stats"
    grep -qx 'valid_superpermutation: 1' "$output/recipe.stats"
    actual_length=$(sed -n 's/^length: //p' "$output/recipe.stats")
    jq -e --argjson actual "$actual_length" '.symbols==$actual' "$output/audit.json" >/dev/null
    validation=paint_waste_reports_byte_equal
fi
jq -n --arg source "$source" --arg validation "$validation" \
    --argjson source_bytes "$(wc -c < "$source")" --argjson recipe_bytes "$(wc -c < "$output/word.kl")" \
    --argjson compressed_recipe_bytes "$(wc -c < "$output/word.kl.xz")" \
    --argjson tool_bytes "$(wc -c < "$tool")" --slurpfile audit "$output/audit.json" \
    --slurpfile compile "$output/compile.time.json" --slurpfile compress "$output/compress.time.json" \
    --slurpfile audit_time "$output/audit.time.json" --slurpfile emit "$output/emit.time.json" --slurpfile baseline "$output/baseline.time.json" \
    '{source:$source,source_archive_bytes:$source_bytes,recipe_text_bytes:$recipe_bytes,recipe_xz_bytes:$compressed_recipe_bytes,
      recipe_archive_ratio:($compressed_recipe_bytes/$source_bytes),recipe_xz_options:["-T1","-6"],tool_bytes:$tool_bytes,audit:$audit[0],
      measurements:{compile:$compile[0],compress:$compress[0],audit:$audit_time[0],emit:$emit[0],baseline_xz:$baseline[0]},
      validation:$validation,memory_scope:"GNU time peak process RSS; not aggregate simultaneous pipeline RSS",
      raw_word_materialized:false,expanded_output_hashed:false}' > "$output/report.json"
if [[ ${4:-} == --paint ]]; then
    jq --slurpfile recipe "$output/paint_recipe.time.json" --slurpfile source "$output/paint_source.time.json" \
        '.measurements.paint_recipe=$recipe[0] | .measurements.paint_source=$source[0]' \
        "$output/report.json" > "$output/report.tmp"
    mv "$output/report.tmp" "$output/report.json"
fi
cat "$output/report.json"
