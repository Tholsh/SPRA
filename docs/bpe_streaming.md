# Streaming byte pairs

Improving on the original byte-pairs code, this layer can now be streamed. The default encoder now utilizes this, using a bounded learning prefix and incremental pair substitution. Use `--whole-file` for the original SPBE encoder, which buffers all runs before emitting the encoded file. Decoding accepts both.

This retains the important transformation: tuple runs become stable byte symbols, frequent pairs become more byte symbols, and xz handles larger repetition. Still, xz on the raw .spra file most often yields lower file size.

## Build and use

```sh
mkdir -p build
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced_codec
g++ -std=c++20 -O2 tools/bpe_layer.cpp -o build/spra-bpe

# Existing corpus archive: streaming SPRA -> BPE -> one continuous xz stream.
bash tools/spra-bpe-xz.sh pack input.spra.xz > output.bpe.xz

# Raw word symbols on stdout, no expanded word file or intermediate spool.
set -o pipefail
bash tools/spra-bpe-xz.sh decode output.bpe.xz | your-consumer

# Optimize over the whole file, buffering all runs.
bash tools/spra-bpe-xz.sh pack --whole-file input.spra.xz > output.bpe.xz

# Recover the original canonical SPRA bytes instead of the word.
bash tools/spra-bpe-xz.sh unpack output.bpe.xz > restored.spra

# Encode a new word. The wrapper measures its header fields first.
bash tools/spra-bpe-xz.sh encode 5 words/5/153/153-recursive.txt > n5.bpe.xz
```

On Windows keep binary pipes in MSYS2/Git Bash or `cmd /c`. MinGW adds `.exe`.
`SPRA_CODEC` and `SPRA_BPE` override the executables. All results go to stdout;
named inputs are preserved. Use different input/output paths and check exit
status. Streaming failures can happen after a prefix has been emitted.

`decode` accepts `--strict` and `--max-symbols COUNT`; the latter is checked by
both layers. `unpack` also accepts `--max-symbols`. BPE encoding options are
`--train-runs COUNT`, `--min-count COUNT`, and `--whole-file`. The training limit is
1..1,048,576 run symbols, default 65,536. Minimum pair frequency defaults to 16.
Whole-file mode defaults to 1,024 and cannot be combined with `--train-runs`.

Arguments after `--` replace the xz compression defaults or go to `xz -d` on
decode/unpack. The defaults are
`-T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0`.
Standard `XZ_OPT`/`XZ_DEFAULTS` still apply. xz is never restarted at record
boundaries, so its dictionary can match across the entire BPE stream.

Default xz accepts incremental input but may hold compressed payload until EOF
on smaller streams. Raw decoding streams independently of this encoding delay.
For earlier compressed output, pass `--block-size=256KiB` along with the other
xz settings after `--`. This restarts the dictionary per xz block and reduces
compression; see the [latency measurements](bpe-stream-benchmark.md#xz-output-latency-check).

## What streams, what remains buffered, and the dictionary

The BPE encoder does not accumulate the entire file or write a temporary file.
It learns from at most 65,536 run symbols by default, then freezes its pair
rules. Each rule needs only one pending byte. A bitmap skips rules which cannot
act on the current symbol. Output is released in blocks of at most 4,096 bytes.
This gives the same substitutions as applying the learned rules sequentially
to a complete sequence, without retaining that sequence.

New run types get inline definitions while dictionary slots remain. With all
256 slots (shared by runs and pair rules) occupied they are emitted as direct run records; the encoder never
grows an unbounded dictionary. Runs longer than 65,536 tuples are split and
merged back by the decoder. Training also ends after at most `16 * train-runs`
logical tuples, apart from one bounded run, or when a new run would exceed the
initial 256-symbol table. The largest pair-count workspace is 65,536 uint64
counters, 512 KiB. These are bounded working buffers, not zero buffering.

After the prefix, run IDs and pair rules remain stable. Independently relearning
small frames was tested and rejected because it harmed xz compression.

The decoder emits SPRA incrementally; the following SPRA decoder emits raw word
symbols incrementally. Tests withhold input EOF and require at least 4,096
output bytes, beyond the maximum metadata-only prefix, before completing input.
Stages run concurrently with pipe backpressure.

For a new word, the wrapper first measures symbol length, logical tuple count
and field width without storing tuples. It supplies these to SPRA so the
header is written immediately and the packed body streams into BPE and xz.
This removes the compressed-body spool, but still requires an initial input
scan. Stdin is copied temporarily unless `--length`, `--tuples` and `--bits`
are supplied; those options skip measurement and enable a single pass.
Mismatches warn on stderr at EOF without changing the supplied header or exit
status. See [encoding details](spra.md). `pack` and raw-word decoding
continue to stream. SPRA's tail reconstruction, strict mode and Halfar's
decode limits are preserved.

## SPBE wire format

The copied SPRA header, seed, tuple meanings, varints and run/pair table entries
are as specified in the [whole-file format description](bpe_layer.md).
Streaming mode uses a mode byte and records following the initial body.

| Part | Contents |
| --- | --- |
| Prefix | ASCII `SPBE`, mode byte `02` (streaming), original SPRA 0.2 header and seed |
| Initial dictionary | Varint T; T run entries; byte R; R pair definitions, with T+R <= 256 |
| Initial body | Varint length; that many symbol bytes, as in whole-file mode |
| Records | Zero or more records listed below, then an explicit END record |

The initial body can be empty. Initial run IDs are 0..T-1; rule k has ID T+k,
and its two children must have lower IDs. Pair rules never change. Later run
definitions receive the next unused ID, after the existing rules and runs.

| Tag | Following bytes | Meaning |
| ---: | --- | --- |
| `00` | None | END; no bytes may follow |
| `01` | Varint length, symbol bytes | More encoded body; length 1..1,048,576 (encoder uses at most 4,096) |
| `02` | Depth byte, signed-offset byte, varint count | Define the next run ID; emits no tuples by itself |
| `03` | Depth byte, signed-offset byte, varint count | Emit this run directly without allocating an ID |

Runs have positive counts and fit the copied SPRA layout. Expanding all body
symbols and direct runs must produce exactly the header's logical tuple count.
The decoder reconstructs canonical SPRA RLE continuously across record
boundaries, including six-bit packing and final padding. Existing canonical
corpus SPRA files therefore round-trip byte for byte. Noncanonical SPRA RLE is
normalized just as in Halfar's original layer.

Before an inline definition or direct run, the encoder flushes pending pair
substitutions. Pair substitutions never cross a direct-run record. Definitions
need no external dictionary or corpus-specific state. Incomplete records,
missing END, extra bytes, invalid symbols/rules/runs, overflowed varints and
logical-count mismatches fail. xz provides the outer integrity check.

## Validation and measurements

```sh
bash tools/tests/test_bpe_layer.sh
```

This includes the imported reference-format checks; 1,201 independent comparisons
between incremental and offline pair substitution; streaming vectors and every
truncation; dictionary-full escapes and inline definitions; large split runs;
corpus round trips; raw-word hashes; preserved limits/strict mode; and live
input tests on Windows. No Python or recipe sources are required.

The [streaming benchmark](bpe-stream-benchmark.md) covers all 36 corpus archives,
with byte-exact SPRA SHA-256 comparisons after decoding. Whole-file mode can improve compression at the cost of memory and output delay.
