# Paint waste: build, run, and validate

For binary compression and decompression, use the separate [SPRA 0.2 codec and xz wrapper](spra.md). The codec always run-length encodes logical tuples before packing. Select four- or six-bit tuple fields with `--bits 4|6`; the signed offsets are relative to N and match the logical tuples printed by this tool.

`tools/paint_waste.cpp` reads one word from stdin, with `1 <= N <= 14`, on a 64-bit platform. By default its alphabet is the first N characters of `123456789ABCDE`. Use `-z` or `--zero-based` for the first N characters of `0123456789ABCD`. Each character is one symbol; letters are uppercase. For example, zero-based N=11 uses `0123456789A`, not decimal numbers separated by spaces. Whitespace is ignored. Multiple lines are concatenated into a single word; a collection of separate words must be checked one word at a time. Four large N=6 collections use tar+xz; N>=9 uses one-based SPRA plus xz. The N=6 `1/` and `base/` examples remain uncompressed. See the [unpacking and streaming guide](corpus.md). See the [corpus layout](../README.md#corpus).

In WSL/Linux Bash, from the repository root, compile with GCC and C++20
(see [setup](tools.md#wsl--linux-setup)):

```sh
mkdir -p build
g++ -std=c++20 -O2 tools/paint_waste.cpp -o build/paint-waste
set -o pipefail
./build/paint-waste -s 5 < words/5/153/153-recursive.txt
```

For an archived N=6 collection, stream a member with `tar -xOJf` as shown in the [corpus guide](corpus.md).

## Output and flags

```text
paint-waste [-s] [-m] [-z|--zero-based] N < WORD
```

By default, stdout contains the painted word, then `reduced_alphabet_words:`, then `statistics:`. All results go to stdout; errors go to stderr. The final character of each dirty length-N window is marked red using ANSI escape sequences. A dirty window repeats a symbol; a clean window contains every alphabet symbol exactly once.

The reduced alphabet contains one `[depth:offset]` pair per dirty window. Depth counts the immediately preceding clean windows. The offset is `N - copy_distance`, using the nearest earlier occurrence of the dirty symbol, exactly as in `reduced_codec`. Both fields print as decimal numbers, with offsets signed: for example `[3:1]` or `[0:-4]`. Labels do not affect tuple values.

Tuple fields are always eight bits: depth 0..255 and signed offset -128..127. There is no `--bits` option. Pairs are logical tuples: repeated pairs are printed individually, without the binary codec’s RLE markers or packed bytes. A trailing clean run has no pair. Statistics remain decimal.
Painting also works for words outside these eight-bit field ranges. Such pairs have a trailing `!` and produce a warning on stderr; `?` marks a missing previous occurrence or an offset outside -128..127. Oversized depths are printed in full rather than truncated. These marked diagnostic pairs cannot be packed into eight-bit fields. Statistics-only mode does not generate tuple warnings.

Use `-s` for statistics only, without color codes, the word, reduced pairs, or section headings. Flags work before or after N. `-h` and `--help` print usage.

Use `-m` to paint each dirty character with brackets instead of ANSI color. Each dirty character is individually marked `[X]`; consecutive dirty characters appear as `[X][Y]`. Clean characters stay unmarked. The reduced-alphabet pairs and the statistical output are identical to the default output. `-m` changes only the painted word and emits no color escapes.

```sh
printf '%s\n' '123121321' | ./build/paint-waste -m 3
# First line: 12312[1]321
printf '%s\n' '012010210' | ./build/paint-waste -z -m 3
# First line: 01201[0]210
```

Both examples still print `[3:1]` in the reduced alphabet and the same text statistics, including `length: 9` and `valid_superpermutation: 1`. Combining `-s` and `-m` prints only the usual statistics: `-s` suppresses the painted word, so there are no brackets. Flags work in either order.

For **N > 9**, output is always statistics-only. Without explicit `-s`, the tool asks `Continue? [y/N]:` through the controlling terminal before reading the word; enter `y` or `yes` to accept. Any other answer cancels with status 1 and no statistics. The answer is read separately from stdin, so piped corpus symbols are never consumed as a reply. When no terminal is available, the tool exits with an instruction to rerun with `-s`. Passing `-s` explicitly accepts statistics-only mode and avoids the prompt, which is suitable for scripts and pipes. `-m` alone does not skip this confirmation. Prompts and errors go to stderr.

Statistics-only mode streams the word instead of retaining it. Exact coverage uses one bit per required permutation: roughly 4.8 MiB at N=11, 57.1 MiB at N=12, 742.3 MiB at N=13, and 10.2 GiB at N=14, plus small input buffers. Allocation or input failures exit with status 1. Clean windows are indexed by rotation class and the position of symbol zero; the class is ranked once per clean run, then successive rotations update the index in constant time. Coverage remains exact.

```sh
# Same valid N=3 word relabeled from 123 to 012.
printf '%s\n' '012010210' | ./build/paint-waste -z -s 3
# Validate the one-based N=11 stream; explicit -s avoids the confirmation prompt.
bash tools/spra-xz.sh decode words/11/43930624/superpermutation-11-43930624.spra.xz | ./build/paint-waste -s 11
```

The N=3 example pipes a literal word. The N=11 pipeline decodes the compressed SPRA archive without creating an intermediate word file.

The N=3 example reports length 9, six distinct permutations, none missing, and `valid_superpermutation: 1`. Changing labels does not change coverage statistics or reduced pairs; painted output retains the selected labels.

The N=11 word was originally checked with zero-based labels and `-z -s 11`; the current one-based stream gives the same coverage: length `43930624`, distinct permutations `39916800`, missing permutations `0`, repeated permutation windows `18816`, and `valid_superpermutation: 1`.

## Statistics

| Field | Meaning |
| --- | --- |
| `length` | Number of input symbols after removing whitespace. |
| `symbols` | Existing name for the same count as `length`. |
| `n` | Alphabet size. |
| `windows` | Number of length-N windows: `max(0, length - N + 1)`. |
| `dirty`, `clean` | Counts of dirty and clean windows. Their sum is `windows`. |
| `max_dirty_run`, `max_clean_run` | Longest consecutive run of each window type, or zero if absent. |
| `dirty_runs`, `clean_runs` | Number of runs of each window type. |
| `required_permutations` | N factorial. |
| `distinct_permutations` | Number of different permutations found among clean windows. |
| `missing_permutations` | Required minus distinct permutations. |
| `repeated_permutation_windows` | Clean windows beyond the first occurrence of each distinct permutation: `clean - distinct_permutations`. |
| `valid_superpermutation` | Binary: `1` when every required permutation occurs, otherwise `0`. |

Validity checks actual distinct coverage, not just the number of clean windows. For example, `1231231` at N=3 has five clean windows but only three different permutations; it is invalid. Validity does not assert that the word is shortest.

## Concrete validation

```sh
printf '%s\n' '123121321' | ./build/paint-waste -s 3
```

```text
length: 9
symbols: 9
n: 3
windows: 7
dirty: 1
clean: 6
max_dirty_run: 1
max_clean_run: 3
dirty_runs: 1
clean_runs: 2
required_permutations: 6
distinct_permutations: 6
missing_permutations: 0
repeated_permutation_windows: 0
valid_superpermutation: 1
```

The six covered permutations are `123`, `231`, `312`, `213`, `132`, and `321`. The window `121` is dirty. The default output also includes the reduced pair `[3:1]` before the statistics.

The corpus example `words/5/153/153-recursive.txt` reports length 153, 120 distinct permutations, and validity 1. Empty input or input shorter than N reports zero windows and validity 0. At N=1, any nonempty valid input covers the sole permutation.

Exit status is 0 for a completed analysis, including a word whose validity is 0. Bad arguments, characters, out-of-alphabet symbols, or input read errors exit with status 1. A script validating coverage must inspect `valid_superpermutation`.

## Regression checks

With Bash and a C++20 compiler available, run from the repository root:

```sh
bash tools/tests/test_reduced_alphabet.sh
```

The checks compile into a temporary directory, compare both alphabets with independent window-set oracles, validate the N=5 corpus example, and check bracket/color statistics agreement, consecutive dirty characters, error output, N=10 through N=13, stream-buffer boundaries, short/default output agreement, whitespace, invalid input, and clean-run depths above 255. See [tool dependencies and build instructions](tools.md).
