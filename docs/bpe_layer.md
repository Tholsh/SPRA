# SPBE: whole-file byte-pair layer

**Integration note:** This is Halfar's whole-file byte-pair code and its
whole-file measurements, imported from PR #2 at commit
`d037d4858cbdbaa4126eb42c45e7b88272301260`. The current default is
[streaming SPBE](bpe_streaming.md). Use `--whole-file` to select this encoder;
decoding supports both modes. Recipes were not imported.

The original optional layer does not change SPRA 0.2: an SPBE
file holds the logical tuples of one SPRA 0.2 file, and decoding it gives back
that SPRA file byte for byte (for every file `reduced_codec encode` writes).
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
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced_codec
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
spra-bpe encode --whole-file [--min-count COUNT] [INPUT|-]   SPRA 0.2 -> whole-file SPBE (default COUNT 1024)
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
but not the word itself: `reduced_codec decode` checks clean and dirty windows
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
| 4 | 1 | Mode byte `0x01` (whole-file). |
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
then the tuples as SPRA 0.2 tokens the way `reduced_codec encode` writes them:
a literal at the start of every maximal run of equal tuples, then markers of
`2^B - 2` repeats, then one marker with the remainder if it is not zero; MSB
first, zero padding.

A decoder refuses: a wrong magic or mode, an invalid SPRA header (version,
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
53 50 42 45 01                        SPBE, whole-file mode
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

## Regression checks

```sh
bash tools/tests/test_bpe_layer.sh
```

The script builds both tools and checks the three vectors above byte for byte,
other `--min-count` values on them, every truncation of a vector, malformed
tables (each refusal above), malformed SPRA input to the encoder, option errors,
words for N=5..9 through the wrapper with both widths, and pack/unpack of every
corpus archive for N=9..11 against `xz -dc`.
