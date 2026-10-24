# Tools without Python

The tools require a C++20 compiler and Bash 4 or newer. The benchmark and archive wrappers
also require xz and standard shell utilities (`sha256sum`, `cmp`, `od`, `head`,
`tr`, `wc`, `mktemp`). On Windows, run the shell commands in MSYS2 with UCRT64 GCC
and `/ucrt64/bin:/usr/bin` on PATH. Keep binary pipelines inside Bash.

Build from the repository root:

```sh
mkdir -p build
g++ -std=c++20 -O2 -Wall -Wextra tools/relabel_words.cpp -o build/relabel-words
g++ -std=c++20 -O2 -Wall -Wextra tools/reduced_codec.cpp -o build/reduced-codec
g++ -std=c++20 -O2 -Wall -Wextra tools/paint_waste.cpp -o build/paint-waste
```

MinGW adds `.exe`. Build outputs belong in the ignored `build/` directory.

## Relabel words

```sh
build/relabel-words --to 1 -n 3 zero-based.txt one-based.txt
build/relabel-words --to 0 -n 3 < one-based.txt > zero-based.txt
bash tools/relabel-folder.sh --to 1 -n 6 --dry-run source-folder converted-folder
bash tools/relabel-folder.sh --to 1 -n 6 source-folder converted-folder
```

`--to 1` expects `0123456789ABC`; `--to 0` expects `123456789ABCD`.
`-n`/`--n` accepts 1..13 (default 13). ASCII whitespace is preserved byte for
byte. Omitted paths or `-` use stdin/stdout; stdin requires stdout. File output
uses a temporary file beside the destination and publishes only after complete
validation. Existing outputs require `--overwrite`; input aliases and symlink
outputs are refused. Streams may emit a prefix before an error; check status.

`--recursive` takes two separate, non-nested directory trees, preflights outputs,
skips symlinks and processes only `.txt` files (case insensitive). `--dry-run`
lists the plan without reading word contents or creating outputs. A failed
folder conversion retains files already completed. Bash's folder launcher uses
`RELABEL_TOOL` to override the executable. PowerShell's launcher accepts
`-Tool PATH`, `-To`, `-N`, `-Source`, `-Destination`, `-DryRun` and `-Overwrite`.

## Benchmark SPRA and xz

```sh
bash tools/benchmark_reduced_codec.sh 5 words/5/153/153-recursive.txt --xz-arg=-6e
bash tools/benchmark_reduced_codec.sh 5 words/5/153/153-recursive.txt \
  --codec build/reduced-codec --baseline-codec /path/to/old-codec \
  --bits 4 6 --xz-arg=-6e --prefix-bytes 1000000
```

N accepts 1..32. Input is raw text or an xz file. `-z`/`--zero-based` selects
zero-based labels. `--prefix-bytes` counts decompressed bytes before whitespace
removal. `--bits` accepts one or both widths; default is `4 6`. Repeat
`--xz-arg=OPTION` for each xz option. `XZ_OPT` and `XZ_DEFAULTS` are cleared.

The JSON report retains word size/hash, xz version/options, direct xz size,
per-layout SPRA and xz sizes, physical/logical token counts, fit failures and
optional baseline results. The runtime field is now `"runtime":"bash"`.
Successful layouts pass byte comparisons after xz and SPRA decoding; the
reported word SHA-256 identifies the normalized input. A non-fitting encoder
is recorded as `fits:false`; other failures return nonzero without publishing
a report. Temporary files are removed on exit. Full benchmarks need temporary
space for raw and intermediate files; streaming the corpus wrapper avoids that
when measurement is unnecessary.

## Regression checks

```sh
bash tools/tests/test_reduced_codec.sh
bash tools/tests/test_reduced_alphabet.sh
bash tools/tests/test_relabel_words.sh
```

Each runner compiles into an OS temporary directory and cleans it on exit.
Set `CXX` to select the compiler. Codec tests use independent C++ wire fixtures
for both widths, legacy decode, RLE limits, malformed headers/markers, padding,
truncation, signed distances, alphabets through N=32, large buffers and the
N * N! decode limit with `--max-symbols`, and the automatic width (four-bit
bytes when they fit, six-bit bytes otherwise). Shell
checks cover CLI errors, paths with spaces, xz failures, N=5..9 corpus samples,
benchmark options/baselines and compressed prefixes. JSON checks use PowerShell
on Windows or `jq` elsewhere.

Paint tests compare statistics against an independent C++ window-set oracle
for both alphabets, test large alphabets, brackets, whitespace, long clean runs,
buffer boundaries and invalid input, verify the N=5 corpus example, and check
consent refusal in a detached process (the Windows process API or `setsid`).
Relabel checks cover exact whitespace, file and stream conversion, safe output
publication, overwrite failures, hardlinks, nested-tree refusal and dry runs.
Native symlink checks run when the host permits creating symlinks.
