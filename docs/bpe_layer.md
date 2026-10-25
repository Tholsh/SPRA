# SPBE 1: Halfar's original whole-file byte-pair layer

**Integration note:** This is Halfar's original SPBE 1 proposal and its
whole-file measurements, imported from PR #2 at commit
`d037d4858cbdbaa4126eb42c45e7b88272301260`. The current default is
[streaming SPBE 2](bpe_streaming.md). Use `--whole-file` to select this encoder;
decoding supports both versions. Recipes were not imported.

The original optional layer does not change SPRA 0.2: an SPBE
file holds the logical tuples of one SPRA 0.2 file, and decoding it gives back
that SPRA file byte for byte (for every file `reduced-codec encode` writes).
This document retains the original format and whole-file benchmark details;
the streaming encoder is documented separately above.

[`tools/bpe_layer.cpp`](../tools/bpe_layer.cpp) converts SPRA 0.2 to SPBE and
back; [`tools/spra-bpe-xz.sh`](../tools/spra-bpe-xz.sh) adds the codec and xz.

## Idea

The tuple stream of a superpermutation repeats on several scales. At N=12, for
example, it is ten copies of `(12,+1)`, then `(12,+2)`, `(0,+0)`, repeated, then
a joining pattern, and so on upwards. SPRA's markers collapse the smallest
scale. SPBE does two more small steps before xz sees the data:

1. Each maximal run of equal logical tuples becomes one byte, an index into a
   table of the distinct runs (61 for an N=12 word; all corpus and public words
   from N=8 to N=14 stay within the 256 a byte allows).
2. Byte-pair encoding: while a byte value is free, the most frequent pair of
   neighbouring bytes is replaced by a new byte, as long as it occurs at least
   1,024 times. This is Re-Pair stopped early. It folds the frequent local
   patterns into single bytes and leaves the long repeats to xz.

A full Re-Pair grammar is about twice as large after xz, because the streams are
near-repeats, which LZ handles better than binary rules. The other layer I
measured, "mlb2" (two levels of repeated blocks that end at the commonest
symbol, each replaced by an index into a table of distinct blocks), comes close
at N=11, N=12 and N=14 but is 10 to 26 % larger on the N=13 words, and its format needs two
nested block tables instead of one table of pairs.

## Build and run

```sh
mkdir -p build
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec
g++ -std=c++20 -O2 tools/bpe_layer.cpp -o build/spra-bpe

# A word to .bpe.xz and back.
bash tools/spra-bpe-xz.sh encode 5 words/5/153/153-recursive.txt > word-n5.bpe.xz
bash tools/spra-bpe-xz.sh decode word-n5.bpe.xz > restored-n5.txt

# A corpus archive to .bpe.xz, and back to raw SPRA bytes.
bash tools/spra-bpe-xz.sh pack words/12/522745531/superpermutation-12-522745531.spra.xz > n12.bpe.xz
bash tools/spra-bpe-xz.sh unpack n12.bpe.xz |
  cmp - <(xz -dc words/12/522745531/superpermutation-12-522745531.spra.xz)
```

```text
spra-bpe encode --whole-file [--min-count COUNT] [INPUT|-]   SPRA 0.2 -> SPBE 1 (default COUNT 1024)
spra-bpe decode [--max-symbols COUNT] [INPUT|-]   SPBE -> SPRA 0.2
spra-bpe-xz.sh encode [--bits 4|6] [-z] N [INPUT|-] [-- XZ_ARGS...]
spra-bpe-xz.sh decode [INPUT|-] [-- XZ_ARGS...]
spra-bpe-xz.sh pack [INPUT|-] [-- XZ_ARGS...]     .spra.xz -> .bpe.xz
spra-bpe-xz.sh unpack [INPUT|-] [-- XZ_ARGS...]   .bpe.xz -> raw SPRA 0.2
```

The wrapper compresses with `xz -T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0`
unless arguments after `--` replace it; when decompressing, those arguments go
to `xz -d`. `SPRA_CODEC` and `SPRA_BPE` override the executables. As with
`spra-xz.sh`, every command writes only its result to stdout, uses `pipefail`,
and output written before an error must be discarded.

`spra-bpe decode` refuses an SPRA header length above N * N! unless
`--max-symbols` allows it. It checks the SPBE structure and the SPRA header,
but not the word itself: `reduced-codec decode` checks clean and dirty windows
when it decodes the result. A word with more than 256 distinct runs cannot be
written as SPBE; the encoder says so, and SPRA plus xz stays the format for it.

## Format

Integers in the copied SPRA header are unsigned 64-bit little-endian, as in
SPRA. The other multibyte integers are unsigned LEB128 varints: seven bits per
byte, lowest group first, the high bit set on every byte except the last; at
most 10 bytes and a value below 2^64.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `SPBE` (`53 50 42 45`). |
| 4 | 1 | SPBE version `0x01`. |
| 5 | `24 + min(length, N-1)` | The SPRA 0.2 header and seed, unchanged. The SPRA version byte must be `0x02`. |

Then, with nothing after them:

1. **Run table.** Varint T (at most 256), then T runs. A run is one byte depth,
   one byte offset (two's complement), and a varint count of at least 1. Symbol
   i < T stands for `count` copies of the logical tuple `(depth, offset)`. Depth
   must fit the SPRA layout (at most `2^B - 1`), the offset its signed B-bit
   range, and depth > 0 with offset 0 is refused (it is not a dirty tuple).
2. **Pair rules.** One byte R with `T + R <= 256`, then R rules of two bytes
   `left, right`. Rule k defines symbol `T + k` as `left` followed by `right`;
   both must be below `T + k`.
3. **Body.** Varint F, then F bytes, each below `T + R`.

Decoding expands every body byte through the rules down to runs. Concatenated,
the runs give the logical tuple sequence; its length must equal the logical
tuple count of the SPRA header. The decoder writes the SPRA header and seed and
then the tuples as SPRA 0.2 tokens the way `reduced-codec encode` writes them:
a literal at the start of every maximal run of equal tuples, then markers of
`2^B - 2` repeats, then one marker with the remainder if it is not zero; MSB
first, zero padding.

A decoder refuses: a wrong magic or version, an invalid SPRA header (version,
layout, flags, N, tuple count above length minus seed, seed symbol outside N),
more than 256 runs and rules, a run, rule or body byte outside its range, runs
that do not add up to the tuple count, a truncated file, extra bytes, and a
varint longer than 10 bytes. Its memory is the two small tables; it streams.

### What the encoder writes

The format allows any tables; this is what `spra-bpe encode` writes and what
the sizes below are measured with.

1. Read the SPRA tokens and merge the logical tuples into maximal runs.
2. Number the distinct runs by decreasing count, ties by first occurrence; the
   body is one byte per run.
3. While `T + R < 256`: count every pair of neighbouring body bytes (inside a
   stretch of equal bytes, pairs are counted without overlap: `aaaa` has two
   `aa`); take the most frequent pair, ties to the smaller `left * 256 + right`;
   stop if it occurs fewer than `--min-count` times (default 1,024); otherwise
   replace it left to right by the new byte `T + R`.

Split markers and repeated literals in the input disappear (they make the same
runs), so a non-canonical RLE body comes back canonical. Copy references are
tuples and stay as they are.

### Example

For `123121321` at N=3 (SPRA body `31`, one tuple `(3,+1)`):

```text
53 50 42 45 01                        SPBE, version 1
53 50 52 41 02 44 00 03 09 00 00 00   the SPRA header ...
00 00 00 00 01 00 00 00 00 00 00 00
00 01                                 ... and seed
01 03 01 01                           one run: depth 3, offset +1, count 1
00                                    no pair rules
01 00                                 body: one byte, symbol 0
```

For `11111` (SPRA body `02 20`, three copies of `(0,+2)`) the run is
`00 02 03` and the rest is the same. With `--min-count 2`, the SPRA body
`02 31 02 31` (header length 18, four tuples) becomes two runs `00 02 01` and
`03 01 01`, one rule `00 01` (symbol 2) and the body `02 02 02`, where the first
`02` is the body length.

## Sizes and times

Every chain below starts from the same SPRA 0.2 file and ends with the same xz
settings, `-T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0`. Bytes are summed
per N. "Corpus" is every `.spra.xz` file in `words/`; "public" is the words of
github.com/jhalfar/superpermutations that are not in the corpus, encoded with
`reduced-codec` (four-bit fields where they fit). Every round trip gave the
original SPRA file byte for byte. Measured with xz 5.8.2 on Windows.

| Words | N | Files | SPRA + xz | mlb2 + xz | SPBE + xz | SPBE / SPRA + xz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| corpus | 9 | 7 | 2,472 | 2,532 | 2,476 | 1.00 |
| corpus | 10 | 6 | 12,312 | 9,836 | 9,692 | 0.79 |
| corpus | 11 | 8 | 96,708 | 44,356 | 43,920 | 0.45 |
| corpus | 12 | 9 | 771,344 | 288,128 | 281,588 | 0.37 |
| corpus | 13 | 5 | 2,516,184 | 660,176 | 559,676 | 0.22 |
| corpus | 14 | 1 | 6,143,360 | 883,476 | 859,588 | 0.14 |
| public | 8 | 8 | 1,736 | 2,308 | 2,004 | 1.15 |
| public | 11 | 6 | 68,728 | 35,632 | 34,804 | 0.51 |
| public | 12 | 6 | 480,016 | 175,724 | 175,304 | 0.37 |
| public | 13 | 9 | 5,576,776 | 1,447,960 | 1,299,776 | 0.23 |

For one word: the public N=13 word 6,747,802,319 is 804,656 bytes as SPRA plus
`xz -9e`, 750,056 with the xz settings above, and 176,928 as SPBE plus xz.
Below N=10 the tables cost more than they save, so plain SPRA plus xz should
stay the format there; a reader could choose by file type.

Times in wall seconds, counted from the SPRA file (the codec's own time is the
same for every chain), on a busy machine at idle priority, so only the ratios
mean much:

| Word | SPRA + xz encode / decode | mlb2 + xz | SPBE + xz |
| --- | ---: | ---: | ---: |
| N=12, 522,736,969 | 2.4 / 0.1 | 0.7 / 0.1 | 0.5 / 0.1 |
| N=13, 6,747,802,319 | 27.6 / 0.3 | 7.4 / 1.0 | 4.1 / 0.9 |
| N=14, 93,904,804,425 | 311 / 2.1 | 94 / 15 | 50 / 13 |

SPBE encodes faster than xz alone because xz then sees a file 50 to 60 times
smaller. Decoding is slower than xz alone, because the layer writes the whole
SPRA file again, but it is short next to decoding the letters themselves.

Memory: the encoder keeps one byte per run in memory, 3.1 GB at its peak for
N=14; the decoder needs about 1 MB besides xz's 257 MiB.

## Regression checks

```sh
bash tools/tests/test_bpe_layer.sh
```

The script builds both tools and checks the three vectors above byte for byte,
other `--min-count` values on them, every truncation of a vector, malformed
tables (each refusal above), malformed SPRA input to the encoder, option errors,
words for N=5..9 through the wrapper with both widths, and pack/unpack of every
corpus archive for N=9..11 against `xz -dc`.
