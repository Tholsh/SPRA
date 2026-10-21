# Superpermutation Reduced Alphabet

The entire purpose of this code is to make enumeration of n=14 and n=15 superpermutations achievable. Currently version `V0.2` is capable of doing this. There is room for improvement here, and space for discussion of information theory.

This code / corpus repo was meant to be imported as a submodule and used in personal repos.

## Corpus

The four large N=6 collections in `words/6/872/` are distributed as `.tar.xz` folder archives. They preserve the original one-based text, filenames, and line endings: **48,292 files, 42.21 MB, become four archives totaling 1.47 MB** (96.53% smaller). The `1/` folder is untouched, and the 1,681 previously loose words now live in the uncompressed `base/` folder. Smaller N=6 collections and all N=5, N=7, and N=8 files retain their existing formats and labels.

N=9 through N=13 use one-based `.spra.xz` words at `words/N/LENGTH/NAME.spra.xz`: SPRA 0.2 followed by `xz -6e`. Four-bit fields are tried first, with six-bit fields used when required. SPRA decode restores symbols without whitespace or a final newline. Word stems and repeated occurrences from different sources are preserved.

[`collection-archives.json`](words/6/872/collection-archives.json) records the N=6 measurements and checksums. [`spra-summary.json`](words/spra-summary.json) and `words/spra-manifest.jsonl.xz` cover the N>=9 words and their verified round-trip hashes.

See the [unpacking and streaming guide](docs/corpus.md). Restore only what you need, or stream raw symbols directly:

```sh
bash docs/unpack-corpus.sh 6 build/unpacked-n6
# Raw stdout, without creating a word file:
tar -xOJf words/6/872/872-treelike.tar.xz 872-treelike/872-00001.txt
bash tools/spra-xz.sh decode words/9/408731/superpermutation-9-408731.spra.xz
```

For PowerShell, use the [launcher](docs/unpack-corpus.ps1). Omit `-N` to unpack all alphabets into the ignored `build/unpacked/` tree:

```powershell
./docs/unpack-corpus.ps1 -N 6 -Destination build/unpacked-n6
```

## Reduced-alphabet binary files

[`tools/reduced_codec.cpp`](tools/reduced_codec.cpp) encodes SPRA **0.2**, applying RLE directly to `(clean-run depth, signed copy offset)` tuples before packing. `(r,0)` repeats the last ordinary tuple r additional times; `(0,0)` stays literal and the maximal-depth zero-offset token is reserved for extensions. RLE is always active. Choose `--bits 4` (default) or `--bits 6`: physical tokens occupy 8 or 12 bits. Decode detects width and also reads old version `0x01` files. Both commands write **only their result to stdout**. Decode restores the normalized word without a final newline. N may be 1 through 32; logical field ranges remain strict.

In a POSIX shell:

Build the codec with a C++20 compiler:
```sh
mkdir -p build
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec
```
Encode an existing uncompressed example:
```sh
./build/reduced-codec encode 5 words/5/153/153-recursive.txt > build/word-n5.spra
./build/reduced-codec encode --bits 6 5 words/5/153/153-recursive.txt > build/word-n5-six.spra
./build/reduced-codec decode build/word-n5.spra > build/restored-n5.txt

# Codec plus xz; arguments after -- are passed to xz.
bash tools/spra-xz.sh encode 5 words/5/153/153-recursive.txt -- -6e > build/word-n5.spra.xz
bash tools/spra-xz.sh decode build/word-n5.spra.xz > build/restored-n5.txt
```

In PowerShell with MinGW GCC, use `cmd /c` for binary-safe redirection:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec.exe
cmd /c '.\build\reduced-codec.exe encode 5 words\5\153\153-recursive.txt > build\word-n5.spra'
cmd /c '.\build\reduced-codec.exe encode --bits 6 5 words\5\153\153-recursive.txt > build\word-n5-six.spra'
cmd /c '.\build\reduced-codec.exe decode build\word-n5.spra > build\restored-n5.txt'
```

Check the exit status before keeping output; errors go to stderr. See the [binary format and run guide](docs/reduced_binary.md) for stdin and xz pipelines, zero-based labels, the complete header and decoding rules, representability limits, and regression checks. The painted-word tool and its historical displayed indices remain separate.

In measurements of the original zero-based 43,930,624-symbol N=11 example, four-bit SPRA 0.2 plus `xz -6e` took **8,012 bytes**, versus 13,540 bytes for the old `V0.1` format with the same settings: **40.8% smaller**. Default-xz size fell from 45,752 to 19,916 bytes. Six-bit 0.2 plus `-6e` took 12,560 bytes. To benchmark the current one-based word, first decode its `.spra.xz` archive, then run `python tools/benchmark_reduced_codec.py 11 restored-n11.txt --xz-arg=-6e`; use `--baseline-codec PATH` for an old/new comparison. All successful benchmark round trips matched SHA-256. The [guide](docs/reduced_binary.md#measurements-02-versus-the-old-format) includes both presets, both widths, and the N=13 prefix. Length counters remain unsigned 64-bit, so complete N=21..32 superpermutations exceed the format's length limit.

## Relabel word files

[`tools/relabel_words.py`](tools/relabel_words.py) converts between zero-based `0123456789ABC` and one-based `123456789ABCD` labels using Python 3.8 or newer. `--to 0` expects one-based input; `--to 1` expects zero-based input. Each uppercase letter is one symbol (`9` becomes `A` when converting to one-based labels). Whitespace, line endings, word length, and filenames are preserved. Optionally use `-n N` to validate the source alphabet size; the default is 13. Only word symbols and ASCII whitespace are accepted.

```powershell
# A single file, in either direction; write to a different output file.
python tools/relabel_words.py --to 0 -n 3 one-based.txt zero-based.txt
python tools/relabel_words.py --to 1 -n 3 zero-based.txt restored.txt
# Convert every .txt file recursively, retaining relative subdirectories.
./docs/unpack-corpus.ps1 -N 5 -Destination build/unpacked-n5
./tools/relabel-folder.ps1 -To 0 -N 5 -Source build/unpacked-n5/5 -Destination build/converted-zero -DryRun
./tools/relabel-folder.ps1 -To 0 -N 5 -Source build/unpacked-n5/5 -Destination build/converted-zero
./tools/relabel-folder.ps1 -To 1 -N 5 -Source build/converted-zero -Destination build/converted-one
```

For Bash (Linux, macOS, WSL, or MSYS2):

```sh
bash docs/unpack-corpus.sh 5 build/unpacked-n5
bash tools/relabel-folder.sh --to 0 -n 5 --dry-run build/unpacked-n5/5 build/converted-zero
bash tools/relabel-folder.sh --to 0 -n 5 build/unpacked-n5/5 build/converted-zero
bash tools/relabel-folder.sh --to 1 -n 5 build/converted-zero build/converted-one
# Decode the already one-based N=11 corpus word.
bash tools/spra-xz.sh decode words/11/43930624/superpermutation-11-43930624.spra.xz > one-based-n11.txt
```

For example, `123121321` becomes `012010210` and converts back exactly. Folder conversion processes plain `.txt` files only, skips symlinks, and requires separate, non-nested input/output folders. Extract compressed archives first. `--dry-run` lists planned conversions without checking word contents. Existing outputs are refused unless you pass `--overwrite` (PowerShell: `-Overwrite`). File outputs are published only after complete validation; a failed file leaves no partial output. A folder run stops at the first error, retaining already completed files. Stream output can contain a valid prefix before an input error, so check the exit status. Run `python tools/relabel_words.py --help` for all options; PowerShell accepts `-Python PATH`, and Bash accepts a `RELABEL_PYTHON` environment override.

## Paint waste

[`tools/paint_waste.cpp`](tools/paint_waste.cpp) builds the `paint-waste` tool. It reads a word from standard input, ignores whitespace, and supports `1 <= N <= 13` on a 64-bit platform. Its default labels are `123456789ABCD`; `-z` / `--zero-based` selects `0123456789ABC`. Use the first N characters of the selected alphabet, with uppercase letters.

For each length-`N` window, it marks the final character red if the window contains repeated symbols. It prints the colored word, reduced-alphabet `[depth:index]` pairs (in hexadecimal), and then decimal statistics to stdout. Statistics include length, clean/dirty windows and runs, distinct/missing permutations, and binary `valid_superpermutation` coverage. Use `-s` to print only statistics. See the [full run guide and field definitions](docs/reduced_alphabet.md).

Run these commands from the repository root with a C++20 compiler such as GCC. In PowerShell with MinGW GCC:

```powershell
g++ -std=c++20 -O2 tools/paint_waste.cpp -o build/paint-waste.exe
Get-Content words/5/153/153-recursive.txt | ./build/paint-waste.exe 5
```

A concrete coverage check, showing four fields from the statistics-only output:

```powershell
'123121321' | ./build/paint-waste.exe -s 3 | Select-String '^(length|distinct_permutations|missing_permutations|valid_superpermutation):'
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
'123121321' | ./build/paint-waste.exe -m 3
# Painted word: 12312[1]321
```

`-s -m` prints only the usual statistics, since `-s` suppresses the word.

For the zero-based version of the same valid word:

```powershell
'012010210' | ./build/paint-waste.exe -z -s 3
```

Above N=9, the tool always uses statistics-only output. Without `-s`, it asks for confirmation through the terminal; explicit `-s` accepts that mode without prompting. If no terminal is available, rerun with `-s`. Large words are streamed; exact coverage at N=13 needs about 742 MiB of RAM. See the [full run guide](docs/reduced_alphabet.md) for alphabet and confirmation details.

On Linux/macOS with GCC:

```sh
g++ -std=c++20 -O2 tools/paint_waste.cpp -o build/paint-waste
./build/paint-waste 5 < words/5/153/153-recursive.txt
```

To use an xz word directly through stdin, without extracting it first (POSIX shell):

```sh
# Make a compressed N=5 example, then pipe it into the tool.
xz < words/5/153/153-recursive.txt > build/word-n5.txt.xz
xz -dc build/word-n5.txt.xz | ./build/paint-waste 5
```

`-dc` decompresses to stdout and preserves the archive. To validate a one-based N=11 SPRA corpus word directly:

```sh
bash tools/spra-xz.sh decode words/11/43930624/superpermutation-11-43930624.spra.xz | ./build/paint-waste -s 11
```

The N=11 course-exchange program takes a word filename and needs extraction first.

## Course exchange search

[`search/course_exchange.cpp`](search/course_exchange.cpp) reconstructs an N=11 word as cyclic courses and searches for savings by changing openings or exchanging courses. Build from the repository root with GNU Make and 64-bit GCC:

```powershell
mingw32-make course_exchange
./build/course_exchange.exe rows.txt circles.txt word-n11.txt out-audit 2 0
```

On Linux/macOS, use `make course_exchange` and `./build/course_exchange`. See the [build and run guide](docs/course_exchange.md) for inputs, search modes, and checkers, or the [complete reproduction](docs/n11/improve_halfar/REPRODUCE.md) for the four-character improvement from Halfar's word.

## License

Our software is free software under the **GNU General Public License, version 3 or later (GPL-3.0-or-later)**. You may use, study, modify, and redistribute it under those terms. Distributed derivatives must preserve the GPL freedoms and provide the corresponding source as required by the license. It comes without warranty. See [LICENSE](LICENSE) for the full terms.

Imported code retains its upstream terms and attribution; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Corpus words and imported documents retain their authorship.
