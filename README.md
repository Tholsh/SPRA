# Superpermutation Reduced Alphabet

The entire purpose of this code is to make enumeration of n=14 and n=15 superpermutations achievable. Currently version `V0.2` is capable of doing this. There is room for improvement here, and space for discussion of information theory.

This code / corpus repo was meant to be imported as a submodule and used in personal repos.

## Corpus

[`words/`](words/) groups words by alphabet size, from `5` through `14`, then by word length: `words/N/LENGTH/`. For example, the recursive N=5 word is [`words/5/153/153-recursive.txt`](words/5/153/153-recursive.txt). Through **N=9**, every `.txt` file contains exactly one superpermutation, using one-based labels `123…N`. This is also where Collections retain their own folders inside the length folder, such as `words/6/872/872-treelike/872-00001.txt` (length and source ordinal). Existing individual word filenames are retained. Repeated occurrences from different sources are preserved.

## Reduced-alphabet binary files

[`tools/reduced_codec.cpp`](tools/reduced_codec.cpp) encodes SPRA **0.2**, applying RLE directly to `(clean-run depth, signed copy offset)` tuples before packing. `(r,0)` repeats the last ordinary tuple r additional times; `(0,0)` stays literal and the maximal-depth zero-offset token is reserved for extensions. RLE is always active. Fields have 4 bits, or 6 when 4 cannot hold the word (encode then prints a note); `--bits 4` or `--bits 6` forces one width. Physical tokens occupy 8 or 12 bits. Decode detects width and also reads old version `0x01` files; `decode --strict` accepts only the canonical file of a word (the one encode writes without `--bits`). Both commands write **only their result to stdout**. Decode restores the normalized word without a final newline. N may be 1 through 32; logical field ranges remain strict.

In a POSIX shell:

Build code
```sh
mkdir -p build
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec
```
Run code
```sh
./build/reduced-codec encode 5 words/5/153/153-recursive.txt > word-n5.spra
./build/reduced-codec encode --bits 6 5 words/5/153/153-recursive.txt > word-n5-six.spra
./build/reduced-codec decode word-n5.spra > restored-n5.txt

# Codec plus xz; arguments after -- are passed to xz.
bash tools/spra-xz.sh encode 5 words/5/153/153-recursive.txt -- -6e > word-n5.spra.xz
bash tools/spra-xz.sh decode word-n5.spra.xz > restored-n5.txt
```

In PowerShell, build with MinGW GCC (see docs/reduced_binary.md) and use `cmd /c` for binary-safe redirection:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec.exe
cmd /c '.\build\reduced-codec.exe encode 5 words\5\153\153-recursive.txt > word-n5.spra'
cmd /c '.\build\reduced-codec.exe encode --bits 6 5 words\5\153\153-recursive.txt > word-n5-six.spra'
cmd /c '.\build\reduced-codec.exe decode word-n5.spra > restored-n5.txt'
```

Check the exit status before keeping output; errors go to stderr. See the [binary format and run guide](docs/reduced_binary.md) for stdin and xz pipelines, zero-based labels, the complete header and decoding rules, representability limits, and regression checks. The painted-word tool and its historical displayed indices remain separate.

The optional [byte-pair layer](docs/bpe_streaming.md), based on Halfar's PR #2,
now streams by default using a bounded training prefix and fixed pair rules.
`bash tools/spra-bpe-xz.sh pack input.spra.xz > output.bpe.xz` converts an
existing archive; `decode output.bpe.xz` streams raw word symbols to stdout.
SPRA itself is unchanged; see [measurements](docs/bpe-stream-benchmark.md).

## Relabel word files


[`tools/relabel_words.cpp`](tools/relabel_words.cpp) converts between zero-based `0123456789ABC` and one-based `123456789ABCD` labels. Build it with `g++ -std=c++20 -O2 tools/relabel_words.cpp -o build/relabel-words` (create `build/` first). See [tools and regression checks](docs/tools.md) for all build commands and dependencies. `--to 0` expects one-based input; `--to 1` expects zero-based input. Each uppercase letter is one symbol (`9` becomes `A` when converting to one-based labels). Whitespace, line endings, word length, and filenames are preserved. Optionally use `-n N` to validate the source alphabet size; the default is 13. Only word symbols and ASCII whitespace are accepted.

```powershell
# A single file, in either direction; write to a different output file.
./build/relabel-words --to 0 -n 3 one-based.txt zero-based.txt
./build/relabel-words --to 1 -n 3 zero-based.txt restored.txt
# Convert every .txt file recursively, retaining relative subdirectories.
./tools/relabel-folder.ps1 -To 0 -N 5 -Source words/5 -Destination converted-zero -DryRun
./tools/relabel-folder.ps1 -To 0 -N 5 -Source words/5 -Destination converted-zero
./tools/relabel-folder.ps1 -To 1 -N 5 -Source converted-zero -Destination converted-one
```

For Bash (Linux, macOS, WSL, or MSYS2):

```sh
bash tools/relabel-folder.sh --to 0 -n 5 --dry-run words/5 converted-zero
bash tools/relabel-folder.sh --to 0 -n 5 words/5 converted-zero
bash tools/relabel-folder.sh --to 1 -n 5 converted-zero converted-one
# Stream a compressed zero-based word into a one-based text file.
xz -dc zero-based-n11.txt.xz | ./build/relabel-words --to 1 -n 11 > one-based-n11.txt
```

For example, `123121321` becomes `012010210` and converts back exactly. Folder conversion processes plain `.txt` files only, skips symlinks, and requires separate, non-nested input/output folders. Extract compressed archives first. `--dry-run` lists planned conversions without checking word contents. Existing outputs are refused unless you pass `--overwrite` (PowerShell: `-Overwrite`). File outputs are published only after complete validation; a failed file leaves no partial output. A folder run stops at the first error, retaining already completed files. Stream output can contain a valid prefix before an input error, so check the exit status. Run `./build/relabel-words --help` for all options; PowerShell accepts `-Tool PATH`, and Bash accepts a `RELABEL_TOOL` environment override.

## Paint waste

[`tools/paint_waste.cpp`](tools/paint_waste.cpp) builds the `paint-waste` tool. It reads a word from standard input, ignores whitespace, and supports `1 <= N <= 13` on a 64-bit platform. Its default labels are `123456789ABCD`; `-z` / `--zero-based` selects `0123456789ABC`. Use the first N characters of the selected alphabet, with uppercase letters.

For each length-`N` window, it marks the final character red if the window contains repeated symbols. It prints the colored word, reduced-alphabet `[depth:index]` pairs (in hexadecimal), and then decimal statistics to stdout. Statistics include length, clean/dirty windows and runs, distinct/missing permutations, and binary `valid_superpermutation` coverage. Use `-s` to print only statistics. See the [full run guide and field definitions](docs/reduced_alphabet.md).

Run these commands from the repository root with a C++20 compiler such as GCC. In PowerShell with MinGW GCC:

```powershell
g++ -std=c++20 -O2 tools/paint_waste.cpp -o paint-waste.exe
Get-Content words/5/153/153-recursive.txt | ./paint-waste.exe 5
```

A concrete coverage check, showing four fields from the statistics-only output:

```powershell
'123121321' | ./paint-waste.exe -s 3 | Select-String '^(length|distinct_permutations|missing_permutations|valid_superpermutation):'
```

```text
length: 9
distinct_permutations: 6
missing_permutations: 0
valid_superpermutation: 1
```

Validity `1` means all `N!` distinct permutations occur. A completed analysis exits successfully even when validity is `0`; inspect that field when validating a word.

Use `-m` to mark dirty characters as `[X]` instead of using color. The reduced alphabet and statistics keep exactly the same format:

```powershell
'123121321' | ./paint-waste.exe -m 3
# Painted word: 12312[1]321
```

`-s -m` prints only the usual statistics, since `-s` suppresses the word.

For the zero-based version of the same valid word:

```powershell
'012010210' | ./paint-waste.exe -z -s 3
```

Above N=9, the tool always uses statistics-only output. Without `-s`, it asks for confirmation through the terminal; explicit `-s` accepts that mode without prompting. If no terminal is available, rerun with `-s`. Large words are streamed; exact coverage at N=13 needs about 742 MiB of RAM. See the [full run guide](docs/reduced_alphabet.md) for alphabet and confirmation details.

On Linux/macOS with GCC:

```sh
g++ -std=c++20 -O2 tools/paint_waste.cpp -o paint-waste
./paint-waste 5 < words/5/153/153-recursive.txt
```

To use an xz word directly through stdin, without extracting it first (POSIX shell):

```sh
# Make a compressed N=5 example, then pipe it into the tool.
xz < words/5/153/153-recursive.txt > word-n5.txt.xz
xz -d < word-n5.txt.xz | ./paint-waste 5
```

`-dc` decompresses to stdout and preserves the archive. To validate a zero-based N=11 corpus word directly:

```sh
xz -dc words/11/43930624/superpermutation-11-43930624.txt.xz | ./paint-waste -z -s 11
```
# Improvement from V0.1

On the complete 43,930,624-symbol N=11 example, four-bit SPRA 0.2 plus `xz -6e` takes **8,012 bytes**, versus 13,540 bytes for the old `V0.1` format with the same settings: **40.8% smaller**. Default-xz size falls from 45,752 to 19,916 bytes. Six-bit 0.2 plus `-6e` takes 12,560 bytes. Reproduce with `bash tools/benchmark_reduced_codec.sh 11 restored-n11.txt --xz-arg=-6e`; use `--baseline-codec PATH` for an old/new comparison. The C++ and shell regression checks are documented in [tools](docs/tools.md); all successful benchmark round trips matched SHA-256. The [guide](docs/reduced_binary.md#measurements-02-versus-the-old-format) and [JSON report](docs/reduced_xz_benchmarks.json) include both presets, both widths, and the N=13 prefix. Length counters remain unsigned 64-bit, so complete N=21..32 superpermutations exceed the format's length limit.

## License

Our software is free software under the **GNU General Public License, version 3 or later (GPL-3.0-or-later)**. You may use, study, modify, and redistribute it under those terms. Distributed derivatives must preserve the GPL freedoms and provide the corresponding source as required by the license. It comes without warranty. See [LICENSE](LICENSE) for the full terms.

Imported code retains its upstream terms and attribution; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Corpus words and imported documents retain their authorship.
