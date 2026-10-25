# Tools

To decompress a folder recursively into `.txt` files, run
`bash tools/decompress-folder.sh SOURCE_DIRECTORY`.
Text files appear beside their archives; existing text files are skipped.
Optionally supply a new output directory to preserve subfolders elsewhere.
See the [corpus guide](corpus.md) for supported archives and examples.

## WSL / Linux setup

Use Ubuntu WSL 2 on Windows and run all builds, programs, pipelines, and tests
inside Linux Bash. These same commands run on a native Linux machine.
Open the repository with VS Code's **WSL: Reopen Folder in WSL** command so
the terminal, compiler, and extensions all run in Ubuntu. The workspace terminal
profile also selects Ubuntu WSL when opened locally on Windows.
If you use the optional LuaLaTeX editor recipe, install Linux TeX with
`sudo apt-get install texlive-luatex`; its command is `lualatex` on PATH.

Install the dependencies inside Ubuntu (once):

```sh
sudo apt-get update
sudo apt-get install -y build-essential git bash xz-utils tar coreutils findutils grep util-linux jq
```

The tools need C++20 and Bash 4+. Tests additionally use jq and `setsid` from
util-linux. Bash pipes, a FIFO, and `timeout` from coreutils check that streaming
output arrives before EOF.
All executables are Linux binaries in the ignored `build/` directory.
Run commands from the repository root. The existing checkout is available at:

```sh
cd /mnt/c/Users/Theo/Documents/Programs/SPRA
git config --local core.autocrlf input
make -j2
make test
```

For an existing Windows checkout, `core.autocrlf=input` makes Git compare CRLF
working files with their normalized LF index entries correctly. It also keeps
future checkouts in LF form. This repository-local setting prevents thousands
of unchanged corpus files from appearing modified when switching to WSL;
it does not rewrite files or stage anything.

A checkout under `~/src/SPRA` uses Linux filesystem semantics and has faster
Git and small-file I/O than `/mnt/c`; the commands are otherwise identical.
Use forward-slash Linux paths and ordinary Bash redirection. For outer pipelines
run `set -o pipefail` and check the exit status before using the output.
Compiler and flag overrides work with `make CXX=clang++ CXXFLAGS='-std=c++20 -O2'`;
use `CXX=clang++ make test` to select a compiler for the test runners as well.

`make` builds the four main tools and the three independent verification programs
in `tools/verification/`. Equivalent main-tool build commands are:

```sh
mkdir -p build
g++ -std=c++20 -O2 -Wall -Wextra tools/relabel_words.cpp -o build/relabel-words
g++ -std=c++20 -O2 -Wall -Wextra tools/reduced_codec.cpp -o build/reduced_codec
g++ -std=c++20 -O2 -Wall -Wextra tools/paint_waste.cpp -o build/paint-waste
g++ -std=c++20 -O2 -Wall -Wextra tools/bpe_layer.cpp -o build/spra-bpe
```

Build outputs belong in the ignored `build/` directory.

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
`RELABEL_TOOL` to override the executable.

## Benchmark SPRA and xz

```sh
bash tools/benchmark_reduced_codec.sh 5 words/5/153/153-recursive.txt --xz-arg=-6e
bash tools/benchmark_reduced_codec.sh 5 words/5/153/153-recursive.txt \
  --codec build/reduced_codec --baseline-codec /path/to/old-codec \
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

`make test` runs the literal verifier's self-test and all four suites below.

```sh
bash tools/tests/test_reduced_codec.sh
bash tools/tests/test_reduced_alphabet.sh
bash tools/tests/test_relabel_words.sh
bash tools/tests/test_bpe_layer.sh
```

Each runner compiles into an OS temporary directory and cleans it on exit.
Set `CXX` to select the compiler. Codec tests use independent C++ wire fixtures
for both widths, legacy decode, RLE limits, malformed headers/markers, padding,
truncation, signed distances, alphabets through N=32, large buffers and the
N * N! decode limit with `--max-symbols`, the automatic width (four-bit
bytes when they fit, six-bit bytes otherwise) and `--strict` (encoder output
passes; split markers, repeated literals, far copies and needless six-bit
fields are refused). Shell
checks cover CLI errors, paths with spaces, xz failures, N=5..9 corpus samples,
benchmark options/baselines and compressed prefixes, and the wrapper's
default xz settings. JSON checks use `jq`.

Paint tests compare statistics against an independent C++ window-set oracle
for both alphabets, test large alphabets, brackets, whitespace, long clean runs,
buffer boundaries and invalid input, verify the N=5 corpus example, and check
consent refusal in a detached Linux process using `setsid`.
Relabel checks cover exact whitespace, file and stream conversion, safe output
publication, overwrite failures, hardlinks, nested-tree refusal and dry runs.
Native symlink checks run when the host permits creating symlinks.
