# SPRA 0.2: tuple RLE and xz

[`tools/reduced_codec.cpp`](../tools/reduced_codec.cpp) transforms normalized words
into SPRA **0.2**, with mandatory run-length encoding of logical tuples. Fields
for unsigned clean-run depth and signed copy offset have 4 or 6 bits. Without
`--bits`, encode writes four-bit fields and falls back to six-bit fields when
four bits cannot hold the word; `--bits 4` or `--bits 6` forces one width.
Physical tokens occupy 8 or 12 packed bits. RLE happens **before packing**,
so six-bit tuples are compared as complete tuples, not as bytes.

Encoding always writes header version byte `0x02`; there is no legacy-output or
RLE-disable option. Decode also accepts `0x01` archives, previously documented as
version 1. Width and alphabet mode are detected from the header.

## Build and run

From the repository root, with a C++20 compiler:

```sh
mkdir -p build
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec
./build/reduced-codec encode 5 words/5/153/153-recursive.txt > word-n5.spra
./build/reduced-codec encode --bits 6 5 words/5/153/153-recursive.txt > word-n5-six.spra
./build/reduced-codec decode word-n5.spra > restored-n5.txt
```

On Windows, create `build/` and compile with MinGW GCC using
`g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec.exe`.
Alternatively compile directly into the repository root:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -pedantic tools/reduced_codec.cpp -o reduced-codec
```

The interface is unchanged:

```text
reduced-codec encode [--bits 4|6] [-z|--zero-based] N [INPUT|-]
reduced-codec decode [--max-symbols COUNT] [INPUT|-]
```

Omitted `INPUT` or `-` reads stdin. `--bits=4` and `--bits=6` are accepted.
Both commands write only their result to stdout. Errors go to stderr, returning
status 1; success returns 0 and is quiet, except for the one-line note of
encode when it falls back to six-bit fields. Decode writes no final newline.

Decode refuses a header length above N * N! before writing anything. N * N! is
the length of all N! permutations written one after another, so no useful word
is longer. `--max-symbols COUNT` (or `--max-symbols=COUNT`) replaces that limit
in either direction: lower it for files from someone else, raise it for long
test words. Without a limit, a 24-byte file with N=1 and length 2^64-1 makes
the decoder write clean steps until the disk is full, and xz shrinks such a
file to almost nothing. The default only catches impossible lengths: at N=13 it
still allows 80,951,270,400 symbols, and from N=20 on N * N! exceeds the 64-bit
length field, so only `--max-symbols` limits those files.

Input labels are the first N characters of `123456789ABCDEFGHIJKLMNOPQRSTUVW`,
or `0123456789ABCDEFGHIJKLMNOPQRSTUV` with `-z`, for N=1..32. Each uppercase
letter is one symbol. ASCII spaces, tabs, CR, LF, form feeds, and vertical tabs
are ignored; other bytes outside the selected alphabet are rejected. Several
lines are concatenated. Decoding restores the normalized word, **not its
whitespace**. Permutation coverage is not checked by the codec.

### Bash wrapper, including `xz -6e`

[`tools/spra-xz.sh`](../tools/spra-xz.sh) combines the codec and xz:

```sh
bash tools/spra-xz.sh encode 5 words/5/153/153-recursive.txt -- -6e > word-n5.spra.xz
bash tools/spra-xz.sh decode word-n5.spra.xz > restored-n5.txt

# Six-bit tuples; decode the one-based corpus archive into the wrapper.
set -o pipefail
bash tools/spra-xz.sh decode words/11/43930624/superpermutation-11-43930624.spra.xz |
  bash tools/spra-xz.sh encode --bits 6 11 -- -6e > word-n11-six.spra.xz
bash tools/spra-xz.sh decode word-n11-six.spra.xz > restored-n11.txt
```

Its interface is:

```text
spra-xz.sh encode [CODEC_OPTIONS] N [INPUT|-] [-- XZ_ARGS...]
spra-xz.sh decode [--max-symbols COUNT] [INPUT|-] [-- XZ_ARGS...]
```

Decode options before `--` go to `reduced-codec decode`.

Arguments after `--` are passed separately and unchanged to xz; for example,
`-- -6e --check=crc32`. Omit them to use xz defaults. Decode also accepts xz
options after `--`, such as `--memlimit-decompress=64MiB`. Named source files are
preserved; the wrapper writes to stdout and does not open an output filename.
It requires Bash and xz on PATH. `SPRA_CODEC=/path/to/reduced-codec` overrides
codec discovery; otherwise it searches `build/` then the repository root,
including `.exe` names. Script discovery is independent of the current directory.

The wrapper uses `pipefail`, so failure of either stage returns a nonzero
status. When adding an outer pipeline, also enable `pipefail` in the calling
shell. Discard redirected output after any failure: xz may have written an empty
archive or the decoder may have written a prefix before the error was detected.
Use different input and output paths; shell redirection truncates its target
before the wrapper starts. Standard `XZ_DEFAULTS` and `XZ_OPT` settings apply to
the wrapper; the benchmark clears those variables for reproducible comparisons.

The equivalent explicit pipeline is:

```sh
set -o pipefail
./build/reduced-codec encode 5 words/5/153/153-recursive.txt | xz -6e > word-n5.spra.xz
xz -dc word-n5.spra.xz | ./build/reduced-codec decode > restored-n5.txt
```

### PowerShell binary redirection

Use Bash through MSYS2/Git Bash, or use `cmd /c` for byte-preserving redirection.
Do not pass SPRA bytes through `Get-Content`, `Out-File`, or a text encoding.
The codec sets its Windows streams to binary mode itself.

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec.exe
cmd /c '.\build\reduced-codec.exe encode 5 words\5\153\153-recursive.txt > word-n5.spra'
if ($LASTEXITCODE -ne 0) { throw 'Encoding failed; discard output.' }
cmd /c 'xz -6e < word-n5.spra > word-n5.spra.xz'
if ($LASTEXITCODE -ne 0) { throw 'Compression failed; discard output.' }
cmd /c 'xz -d < word-n5.spra.xz > unpacked.spra'
if ($LASTEXITCODE -ne 0) { throw 'Decompression failed; discard output.' }
cmd /c '.\build\reduced-codec.exe decode unpacked.spra > restored-n5.txt'
if ($LASTEXITCODE -ne 0) { throw 'Decoding failed; discard output.' }
```

## Logical tuples and repeat markers

The seed is the first `min(decoded_length, N-1)` symbol IDs. Each length-N window
is clean when all its symbols are distinct, and dirty otherwise. For each dirty
window the logical tuple is:

```text
depth:  number of preceding consecutive clean windows
offset: N - copy_distance, encoded as signed B-bit two's complement
```

The encoder chooses the nearest earlier occurrence of the dirty symbol. The
decoder permits any available backward reference that produces a dirty window.
Clean steps are unique: emit the symbol missing from the preceding N-1 symbols.
Once a clean window is established, further clean steps copy at distance N.
After the last logical tuple, decoded length determines the trailing clean run.
The signed offset differs from the historical diagnostic index printed by
`paint-waste`; those printed pairs cannot be packed directly into SPRA.

Positive depth with offset zero cannot describe a dirty tuple: generating a clean
window and then copying at distance N continues the clean sequence. SPRA 0.2
uses those impossible combinations as control tokens, without removing any valid
ordinary depth or offset values. **`(0,0)` is still an ordinary literal tuple.**

Let `M = 2^B - 1`:

| Physical token | Meaning |
| --- | --- |
| Any valid ordinary tuple, including `(0,0)` | Execute once and remember it. |
| `(r,0)`, `1 <= r < M` | Execute the last ordinary tuple **r additional times**. |
| `(M,0)` | Reserved extension opcode; currently rejected. |

For four-bit fields a marker stores 1..14 additional copies; for six-bit fields,
1..62. A marker has no payload. Each repetition executes the remembered tuple
against the updated word state, checking clean steps, references, and length.
The remembered tuple survives a marker, so consecutive markers can represent a
long run. A marker cannot be the first physical token. Block-repeat commands
are not implemented; the extension opcode remains available for future work.

The encoder emits a literal at the start of each maximal identical-tuple run,
followed by maximal-count markers and any remaining nonzero count. Singletons
stay literal. For example:

```text
(11,+1), (8,0)  ->  nine logical copies of (11,+1)
```

The comparison uses the full `(depth, offset)` tuple. RLE is always active and
needs only the previous tuple and one bounded counter. It never increases the
number of physical tokens relative to logical tuples. Final xz size can still
increase on some inputs because the byte patterns and xz parsing change.

## Wire format

Multibyte header integers are unsigned 64-bit little-endian. Offsets are bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `SPRA` (`53 50 52 41`). |
| 4 | 1 | `0x02` for SPRA 0.2; decode also accepts old `0x01`. |
| 5 | 1 | `0x44` or `0x66`: B=4 or B=6 bits per field. |
| 6 | 1 | Bit 0 selects zero-based labels; remaining bits must be zero. |
| 7 | 1 | N, from 1 through 32. |
| 8 | 8 | Decoded symbol count, excluding whitespace. |
| 16 | 8 | **Expanded logical tuple count**, excluding control tokens. |
| 24 | `min(decoded_length, N-1)` | Seed IDs, one byte each, in `0..N-1`. |
| After seed | Variable | Packed physical tokens. |

There is no RLE header flag: version `0x02` always uses these token semantics.
Version `0x01` instead treats all tokens as ordinary tuples and has no markers.
Old encoders are not selectable; old decoders reject a `0x02` file.

Decode reads physical tokens until exactly the header's logical tuple count has
been executed. A marker that would overshoot it is rejected. If P physical
tokens were consumed, body size is `ceil(2 * B * P / 8)`, **not** a function of
logical tuple count alone. Empty words have a 24-byte header. N=1 needs no seed
or logical tuples; its decoded length determines its word.

Tokens form an MSB-first bit stream, independent of header endianness:

```text
token = (depth << B) | (offset & ((1 << B) - 1))
```

For B=4 each physical token is one byte. For B=6 two successive 12-bit values
`a,b` occupy:

```text
a >> 4, ((a & 15) << 4) | (b >> 8), b & 255
```

An odd final physical token occupies `a >> 4, (a & 15) << 4`; its unused low four
bits must be zero. Padding follows the **physical** token stream. Extra bytes,
truncation, unknown versions/layouts, reserved flags/opcodes, invalid seeds,
impossible clean/dirty steps, and invalid references are rejected. One invocation
decodes one file; concatenated SPRA files are not accepted.

### Independent format examples

For `123121321` at N=3, four-bit output is:

```text
53 50 52 41 02 44 00 03   magic, version, layout, flags, N
09 00 00 00 00 00 00 00   decoded length = 9
01 00 00 00 00 00 00 00   logical tuple count = 1
00 01                     seed IDs for "12"
31                        literal depth 3, offset +1
```

Decode emits seed `12`, three clean steps `312`, dirty copy `1`, then trailing
clean steps `321`. Six-bit mode uses layout `66` and body bytes `0C 10`.

For `11111` at N=3, seed IDs are `00 00`, decoded length is 5, and logical tuple
count is 3. Four-bit body `02 20` means literal `(0,+2)` followed by two extra
copies. Six-bit body is `00 20 80`, encoding literal `0x002` and marker `0x080`.

## Measurements: 0.2 versus the old format

The [JSON report](reduced_xz_benchmarks.json) records **16 comparisons**, dated
2026-10-20, using xz 5.4.3 and Python 3.10.12. Each selected input was tested
with both widths, both default xz and `-6e`, and a separately compiled old codec
from the recorded Git commit. No environment overrides were used. All successful
xz and codec round trips matched normalized input sizes and SHA-256 hashes.
Sizes include the SPRA header/seed and the complete xz container and checksum.

With **default xz**, all sizes below are bytes:

| N | Word length | Direct xz | Old four-bit | 0.2 four-bit | 0.2 six-bit |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 153 | 132 | 96 | 96 | 104 |
| 6 | 872 | 308 | 140 | 148 | 176 |
| 7 | 5,913 | 640 | 132 | 124 | 148 |
| 8 | 46,181 | 3,420 | 264 | 212 | 244 |
| 9 | 408,731 | 23,564 | 856 | 540 | 692 |
| 10 | 4,034,889 | 204,820 | 5,192 | 2,720 | 3,704 |
| 11 | 43,930,624 | 1,968,180 | 45,752 | 19,916 | 27,204 |

With **`xz -6e`**, comparing the same settings on each format:

| N | Old four-bit | 0.2 four-bit | Old six-bit | 0.2 six-bit |
| ---: | ---: | ---: | ---: | ---: |
| 5 | 96 | 96 | 104 | 104 |
| 6 | 140 | 148 | 156 | 176 |
| 7 | 124 | 120 | 152 | 148 |
| 8 | 208 | 208 | 224 | 228 |
| 9 | 512 | 448 | 708 | 564 |
| 10 | 2,340 | 2,008 | 3,052 | 2,496 |
| 11 | 13,540 | 8,012 | 21,900 | 12,560 |

The examples are `153-recursive`, `872-treelike/872-00001`, `5913-palindromic`,
`superperm8_46181_1`, `superperm9_408731_19_canonical`,
`superpermutation-10-4034889`, and `superpermutation-11-43930624`. N=10 and N=11
use zero-based labels. These are construction-dependent measurements: RLE stays
mandatory even where it ties or increases compressed size, such as N=6 here.

### Full N=11 result

The 43,930,624-symbol word contains 3,994,998 logical tuples, represented by
1,511,458 physical tokens. Four-bit SPRA shrinks from 3,995,032 to **1,511,492
bytes** before xz; six-bit SPRA shrinks from 5,992,531 to **2,267,221 bytes**.

Four-bit plus `-6e` is **8,012 bytes**, 40.8% below the old format with the same
settings (13,540 bytes), and 82.5% below the old format with default xz (45,752
bytes). Default-xz four-bit compression improves 56.5%, to 19,916 bytes. Direct
compression of the normalized text with `-6e` takes 1,784,596 bytes. All four
codec combinations restore SHA-256:

```text
0f3b2dcf607aaf3e7985c9daf2d87c07f8947522490dfea90b05a90c7fd5aa6b
```

### N=13 range-limit prefix

The **first 16 MiB only** of the original zero-based N=13 word, now stored as
`words/13/6747918058/superpermutation-13-6747918058.spra.xz`, requires six-bit
fields. Four-bit encoding rejects copy distance 3 at symbol 9,374,975 (offset
+10); a later distance 23 also lies outside its range. RLE does not change those
logical field limits. The 0.2 six-bit file is 610,377 bytes, compared with
1,949,766 before RLE. Its xz size is **508 bytes with defaults** or **456 with
`-6e`**, versus old-format sizes 736 and 660, respectively. This is a verified
prefix round trip, not a test of the complete N=13 word or its coverage.

### Reproduce comparisons

```sh
bash tools/benchmark_reduced_codec.sh 8 words/8/46181/superperm8_46181_1.txt
bash tools/spra-xz.sh decode words/11/43930624/superpermutation-11-43930624.spra.xz > restored-n11.txt
bash tools/benchmark_reduced_codec.sh 11 restored-n11.txt --xz-arg=-6e
bash tools/spra-xz.sh decode words/13/6747918058/superpermutation-13-6747918058.spra.xz > restored-n13.txt
bash tools/benchmark_reduced_codec.sh --prefix-bytes 16777216 13 restored-n13.txt --xz-arg=-6e
```

The script prefers `build/reduced-codec[.exe]`, falling back to the repository
root. Use `--codec PATH` to choose another binary, `--bits 6` for one width,
and repeated `--xz-arg=OPTION` arguments to configure compression. A rejected
width is reported with its error. Input archives are decompressed and normalized
before recompression; existing archive sizes are not used as the direct baseline.
Temporary files bound memory use. The script clears `XZ_DEFAULTS` and `XZ_OPT`.
These commands use the current one-based corpus. To reproduce the historical
zero-based measurements exactly, relabel the decoded input with
`build/relabel-words --to 0 -n N` and benchmark with `-z`.

To reproduce an old/new comparison, compile `tools/reduced_codec.cpp` from the
`baseline_commit` pinned in the JSON report into a separate executable, then add
`--baseline-codec PATH`. Both codecs receive identical normalized words and xz
arguments, and both are independently decoded and checked. Output records the
version byte, logical count, physical token count, raw size, and archive size.

## Limits and resource use

RLE preserves the original logical field limits:

| Bits per field | Recorded depth | Signed offset | Copy distance |
| ---: | ---: | ---: | --- |
| 4 | 0..15 | -8..+7 | `max(1,N-7)..N+8` |
| 6 | 0..63 | -32..+31 | `max(1,N-31)..N+32` |

Every dirty symbol must already have appeared, and its nearest-reference offset
must fit. A first appearance inside a dirty window is rejected. There are no
literal-symbol or extended-depth escapes. A clean run before a dirty symbol must
fit the chosen field; an arbitrary trailing clean run is represented by decoded
length. Values never wrap or truncate.

Without `--bits`, encode checks every tuple against both widths in one pass. If
all tuples fit four-bit fields it writes exactly the bytes of `--bits 4`;
otherwise it writes exactly the bytes of `--bits 6` and prints one line on
stderr, `reduced-codec: note: four-bit fields cannot encode this word (...)`,
naming the first tuple out of the four-bit range. The exit status is still 0.
If six bits do not fit either, encode fails with the six-bit error. With
`--bits 4` or `--bits 6` there is no fallback: a tuple out of range is an error,
as before.

Header counts and word positions remain unsigned 64-bit. Although N=32 is a
supported alphabet, complete N=21..32 superpermutations exceed the length field:
even 21! is greater than `2^64-1`. This version does not widen those counters.

The encoder checks all input before publishing bytes, spooling the RLE body to a
temporary file to determine header counts. Memory is fixed buffers plus O(N)
state. Temporary disk use is the packed **physical** body; without `--bits` the
spool uses six-bit fields and is re-packed when four bits fit, so it can be
up to 1.5 times the final body. Decode streams without
spooling the word. SPRA itself has no checksum; xz supplies its outer integrity
check. Structural validity and archive integrity do not establish permutation
coverage.

## Regression checks

```sh
bash tools/tests/test_reduced_codec.sh
bash tools/tests/test_reduced_alphabet.sh
bash tools/tests/test_relabel_words.sh
```

The [C++ fixtures and shell runners](tools.md) replace the Python suite. Checks cover independent 0.2 vectors,
old `0x01` decode, both widths and label modes, repeated zero tuples, maximal
counts and chained markers, malformed controls, logical and decoded length
limits, semantic validation during replay, physical padding, truncation and
extra bytes, N=32 boundaries, corpus round trips, and large buffers. Wrapper
checks exercise stdin and named paths with spaces, `-6e` and extra xz options,
input preservation, and failures in either pipeline stage. Bash is needed for
wrapper tests. The benchmark
option/baseline path and the existing painted-word tests are also checked.
