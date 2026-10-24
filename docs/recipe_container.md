# Recipe containers (proposal)

A recipe container (`.sprc`) stores a constructed superpermutation as the choices its construction consumes
instead of its letters. The decoder runs the construction, which is pinned by the SHA-256 of its source, and checks
the length and SHA-256 of the letters it gets. It is a separate format next to SPRA, not an SPRA layer: SPRA
stores any word; a recipe container stores only words that a known construction makes, and it can carry an SPRA
file as a fallback.

The format, the tools and the example files below are a proposal. Names, the extension and the layout are open to
change.

## Use

```sh
PY=python3   # Python 3, standard library only
$PY tools/recipe/recipe.py info   words/13/6747801835/superpermutation-13-6747801835.sprc
$PY tools/recipe/recipe.py decode words/12/522736969/superpermutation-12-522736969.sprc -o n12.txt
$PY tools/recipe/recipe.py decode words/14/93904804425/superpermutation-14-93904804425.sprc        # check only
$PY tools/recipe/recipe.py decode FILE.sprc -o - | ./build/paint-waste -z -s 12                    # stream
bash tools/tests/test_recipe_container.sh            # n = 10, 11, 12 and the refusal checks, about 1 minute
bash tools/tests/test_recipe_container.sh --slow     # also n = 13 and n = 14
```

`decode` writes the letters with no final newline, like `spra-xz.sh decode`. Without `-o` it only checks length
and hash. Exit status: 0 ok, 1 usage or I/O error, 2 container refused, 3 the word does not match the expected
length or SHA-256 (an output file is removed), 4 a generator step failed. `--work DIR` puts the temporary files in
DIR, and `--keep` keeps them. n = 13 needs about 14 GB of temporary disk, because the generator writes the base word
and the word as files. n = 14 streams.

The decoder needs Python 3 for the generators of these words (they are Python, plus `gen14.c`) and a C compiler
for n = 14 (`CC`, default `gcc`). The fallback path needs the SPRA codec (`SPRA_CODEC`, `build/reduced-codec`, or it
builds `tools/reduced_codec.cpp` with `CXX`).

## Measured sizes

Bytes. Every size comes from an exact round trip. For the recipe container that means the SHA-256 of the decoded
letters equals the published one. For the other columns it means the decoded SPRA file is identical. Columns 3 to
7 are from my lab runs (`xz -9e`; "mlb2" is a block layer I measured earlier, "BPE" is byte-pair encoding of the
SPRA run bytes, the idea behind `docs/bpe_layer.md`).

| word | corpus `.spra.xz` | SPRA + `xz -9e` | mlb2 + xz | full Re-Pair + xz | rule + exceptions + xz | BPE + xz | **`.sprc`** | `.sprc` / SPRA + xz | `.sprc` / BPE |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| n = 10, 4,034,855 | 2,176 | 2,176 | 1,732 | 2,704 | 2,340 | 1,700 | **1,365** | 0.63 | 0.80 |
| n = 11, 43,930,578 | not in corpus | 15,392 | 7,392 | 13,964 | 10,200 | 6,920 | **4,726** | 0.31 | 0.68 |
| n = 12, 522,736,969 | 112,676 | 99,976 | 39,208 | 83,060 | 55,052 | 38,176 | **9,199** | 0.092 | 0.24 |
| n = 13, 6,747,801,835 | not in corpus | 811,268 | 197,644 | 366,576 | 276,460 | 178,432 | **41,628** | 0.051 | 0.23 |
| n = 14, 93,904,804,425 | 9,679,980 | 7,830,260 | 883,252 | not run | not run | 858,488 | **23,089** | 0.0029 | 0.027 |

Decoding the containers (one thread, idle priority, peak committed memory of the decoder and its children):

| word | decode | peak memory | what dominates |
|---|---:|---:|---|
| n = 10 | below 1 s | 0.09 GB | |
| n = 11 | 3 s | 0.21 GB | geng |
| n = 12 | 23 s | 0.75 GB | geng 19 s |
| n = 13 | 179 s | 2.00 GB | geng with `--noliteral` |
| n = 14 | 207 s | 0.49 GB | gen14 writing 93.9 G letters into the hash |

What is in the containers (parts before xz; the header adds about 120 bytes, the step list 136 to 251 bytes before xz):

| word | selection data | plan (PLC2) | total after xz |
|---|---|---:|---:|
| n = 10 | 8-symbol selection 1,444, diff 197 | 2,613 | 1,365 |
| n = 11 | 8-symbol selection, diff of 56 rows 341, (7, 2) block 116, two connection files 16,010 | 5,984 | 4,726 |
| n = 12 | 8-symbol selection, diff 197, (7, 3) block 1,012 | 26,482 | 9,199 |
| n = 13 | 8-symbol selection, diff 197, (7, 3) block 1,012, (7, 4) block 10,084 | 177,913 | 41,628 |
| n = 14 | 8-symbol selection, diff 197, (7, 3) block 1,012 | 367,615 | 23,089 |

From n = 12 on the plan is most of the container. The n = 14 plan is smaller after xz than the n = 13 plan because it
is a lift of it. The rule + exceptions column is the best I got from describing the letters by rules: Pantone-type
words keep their rules only at the level of single cycles, and the substitution behind them acts on selections,
which is what a recipe stores. Linear recurrences give nothing (the SPRA streams have linear complexity about N/2).

## Layout

All integers are unsigned little-endian. A container is one header, one payload and one optional fallback, in
this order, and ends exactly there.

| Field | Size | Contents |
|---|---:|---|
| magic | 4 | ASCII `SPRC` (`53 50 52 43`) |
| version | 1 | `0x01` |
| n | 1 | number of symbols, 1 to 32 |
| flags | 1 | 0; other values are refused |
| alphabet | n | the letters of the word as the generator writes them, n distinct bytes `0x21`..`0x7E` |
| length | 8 | number of letters of the word |
| word SHA-256 | 32 | SHA-256 of the letters: no whitespace, no line end |
| family length | 1 | 1 to 64 |
| family | that many | ASCII construction family, e.g. `pantone` |
| generator count | 1 | G, 1 to 16 |
| G times: id length | 1 | 1 to 64 |
| G times: id | that many | ASCII generator id with version, e.g. `pantone-py/1` |
| G times: source SHA-256 | 32 | SHA-256 of the generator's source manifest (below) |
| payload method | 1 | 1 = xz |
| payload length | 8 | at most 64 MiB |
| payload | that many | exactly one xz stream; decompressed at most 64 MiB |
| fallback kind | 1 | 0 = none, 1 = SPRA 0.2 + xz |
| fallback length | 8 | only when kind is not 0; at most 4 GiB |
| fallback | that many | a `.spra.xz` file, byte for byte |

The examples have alphabet `0123456789ABCD` cut to n, because their generators write zero-based letters. The
corpus `.spra.xz` files are one-based. The hash always belongs to the letters with the stated alphabet.

### Generators and source hashes

A generator id names a fixed program that the reader has as source. The container names it and gives the SHA-256
of its source. A reader runs no code from the container. The manifest of a generator is the text made of one line
per source file, in the order listed below:

```text
<SHA-256 of the file, lowercase hex>  <file name>\n
```

(two spaces; the file hash is taken after every CR LF is replaced by LF, so a checkout with CRLF line ends gives the
same hash). The source SHA-256 is the SHA-256 of that text.

| id | family | files (in `tools/recipe/gen/`) | source SHA-256 |
|---|---|---|---|
| `pantone-py/1` | `pantone` | `pantone_py/`: applyplan.py, build12.py, gcore.py, gen12.py, geng.py, gregion.py, hybrid3.py | `1bf9c29cc41f3781ec8318477fb731dcb203ce3d2b8969eb152bb8e467a71511` |
| `gen14/1` | `pantone` | `gen14/`: gen14.c | `3e597948abe4c7948cf3edead08199e04f37b44581e6356689b3b8773ad0cb5c` |

`recipe.py hashes` prints them. A reader refuses a container (exit 2, before any step runs) when

* a listed generator id is unknown to it,
* the family of a listed generator is not the family in the header, or
* the source SHA-256 of a listed generator differs from the hash of its own copy of that generator.

The message names the generator and both hashes, and points to `--fallback` when the container has one. A changed
generator is a new id (`pantone-py/2`), never the old id with another hash. Readers may keep older generator
versions next to newer ones so that old containers stay decodable.

### Payload

After xz decompression:

| Field | Size | Contents |
|---|---:|---|
| magic | 4 | ASCII `RCP1` |
| part count | 2 | P |
| P times: name length | 1 | |
| P times: name | that many | `[A-Za-z0-9][A-Za-z0-9._-]{0,63}`; not a Windows device name (CON, NUL, COM1, ...) |
| P times: data length | 4 | |
| P times: data | that many | |

Names are unique. The part `steps` is required. Every other part becomes a file of that name in a fresh, empty
work folder, and the steps refer to files only by these names, so a container cannot name a path.

### Steps

ASCII, one step per line, tokens separated by single spaces, every line ending with LF, no comments:

```text
OP ARG ... [--FLAG]
```

An input name must be a part or the output of an earlier step. An output name must be new. Integer arguments are
decimal. Only the last step may write the output `word`, and it must write it. Every operation belongs either to
the container itself or to one generator. An operation of a generator that the header does not list is refused.

Container operations (defined completely here):

| op | arguments | meaning |
|---|---|---|
| `unpack` | PACK OUT | a selection pack to a selection file |
| `patch` | SEL DIFF OUT | SEL with the loops of a diff replaced |
| `plan` | PLC OUT | a PLC2 part to the plan text |

Operations of `pantone-py/1`. Each one runs the named script of `tools/recipe/gen/pantone_py/` with
`python -E -s`, in the work folder, with the file arguments as paths there. The scripts define the semantics; their
docstrings document them.

| op | arguments | command |
|---|---|---|
| `transport` | SEL k OUT | k transports of SEL (k = 1 to 4): `gcore.read_any`, `gcore.transport` k times, `gcore.write_gsel` |
| `hybrid3` | BASE_M BASE r BLOCK OUT [`--fsd`] | `hybrid3.py BASE_M BASE r BLOCK OUT [--fsd]` (r = 1 to 8) |
| `build12` | SEL11 BASE8 BLOCK4 OUT | `build12.py SEL11 BASE8 BLOCK4 OUT` |
| `gregion` | BASE FALLBACK OUT CONN... | `gregion.py multi BASE FALLBACK OUT CONN... --walks 0:0` |
| `geng` | SEL BASE TABLE [`--noliteral`] | `geng.py SEL BASE --table TABLE [--noliteral]` |
| `applyplan` | BASE TABLE PLAN OUT | `applyplan.py BASE TABLE PLAN OUT`; one final LF of OUT is not part of the word |

Operation of `gen14/1`:

| op | arguments | command |
|---|---|---|
| `gen14` | SEL PLAN OUT | compile `gen14.c` (`$CC -O3`), drop the lines of SEL that start with `#`, run `gen14 plan SEL PLAN --nosha`; standard output is OUT |

A transport (Pantone, section 4) maps every loop (x, v) on K letters with v in {0, K, K - 2} to K loops on K + 1
letters: for j = 0..K-1, y = x[0..j-1] K x[j..K-1], except that for v = K - 2 and j = K - 1, y = x[K-1] x[0..K-2] K.
The new entry is (y, v + 1) on the loop of y when v > 0, and the loop of y without a row when v = 0.

The steps of the n = 13 example:

```text
unpack base8.sel base8
transport base8 1 t9
patch t9 n10.diff n10
unpack b73c.blk b73c
hybrid3 base8 n10 3 b73c selC --fsd
unpack b74.blk b74
build12 selC base8 b74 sel
plan plan.plc plan
geng sel base table --noliteral
applyplan base table plan word
```

In words: Pantone's 8-symbol selection, transported once and changed on 32 loops, is the n = 10 selection. A
(7, 3) block solution put into its 48 blocks gives selection C on 11 symbols. One more transport with a (7, 4) block
solution gives the 12-symbol selection. geng completes it and writes every closed trail once, and the plan says in
which order and with which cuts the trails are written. The other step lists are in `tools/recipe/examples/`.

### Part formats

**Selection files** (outputs of `unpack`, `patch`, `transport`): one line per loop in increasing order, `x T`
with x the K letters of the state at which the row starts (alphabet `0123456789AB...`) and T the number of visible
classes v, or `F`, `S`, `D` for K, K - 2, 0 when every row of a whole selection is one of those. Lines starting with
`#` are comments, and every reader ignores them. A loop is the cyclic order of the K letters, written from letter
0. Loops compare as tuples.

**Selection pack** (`unpack`): bytes K, kind, m, r, then 2 bytes (v, rho) per loop of a fixed list:

* kind 0 (a whole selection): m = r = 0. The list is all (K-1)! loops in increasing order, that is 0 followed by
  the permutations of 1..K-1 in lexicographic order.
* kind 1 (a block solution): m + r = K. The list is the loops in which the letters 0..m-1 stand in the cyclic order
  (0 1 .. m-1), in increasing order: (K-1)!/(m-1)! loops.

v is the number of visible classes (0 = loop without a row, at most K). The row starts at the rotation
L[rho..] L[..rho-1] of the loop L, with rho < K, and rho = 0 when v = 0. The file length must be 4 + 2 times the
list length.

**Diff** (`patch`): byte K, u32 count, then count times (u32 index, byte v, byte rho). The index is into the kind 0
list of K letters. Indices increase strictly. The selection read by `patch` must have K letters.

**PLC2** (`plan`): a plan text split into columns, round trip byte for byte.

| Field | Contents |
|---|---|
| `PLC2` | magic |
| 1 byte | 1 if the text ends with LF |
| varint V, V bytes | the verbatim lines, joined by LF |
| varint L, L bytes | one kind byte per line: `0x00` verbatim, `#` a bare integer, an ASCII letter a typed line |
| L bytes | 1 if the line ended with CR LF, else 0 |
| varint T, T times (byte, varint) | the line types (letter or `#`) and their number of fields |
| per column | byte d, varint length, that many bytes of varints |

Columns are ordered by type byte, then by field index. A varint is unsigned LEB128. When d = 1 the values are
zigzag differences to the previous value of the same column (the first one to 0). Zigzag maps 0, -1, 1, -2 to
0, 1, 2, 3. A typed line is rebuilt as the letter, a space and the fields in decimal separated by spaces, and a
`#` line as the bare decimal. Then CR is appended where flagged, the lines are joined by LF, and LF is appended
when flagged. The encoder takes a line as typed when it is a letter followed by at least one canonical decimal
(no sign, no leading zero), with one number of fields per letter, and as `#` when it is one canonical decimal.
Every other line is verbatim. For each column it keeps the plain or the difference form, whichever xz packs
smaller.

### The word

The output `word` of the last step is the word. The reader counts and hashes it while it streams by. The word
must have exactly the header's length and SHA-256. Otherwise the reader exits with 3 and removes its output file.
`gen14` streams into the hash, so the 94 GB word of n = 14 is never stored unless `-o` asks for it.

### Fallback

Fallback kind 1 is an SPRA 0.2 file after xz, exactly what `spra-xz.sh encode` writes. Its symbol i (one-based or
zero-based labels, from byte 6 of the SPRA header) stands for the letter alphabet[i] of the container. Decoded
this way it must give the same length and SHA-256. `recipe.py decode --fallback` takes this path without running
any generator, so a reader without the generators, or with other versions of them, can still read the word.
`add-fallback` and `extract-fallback` attach and recover the `.spra.xz` bytes. The examples carry no fallback,
because the corpus has the SPRA file of n = 10, 12 and 14 beside them, and a fallback would make the n = 13
container 20 times larger. The test attaches the corpus file of n = 10 as a fallback and checks that it decodes to
the same letters.

## How it relates to SPRA

* SPRA stays the universal format: any word, no construction needed, decoded in seconds by a small C++ program.
* A recipe container is family code plus choices. It is 1.6 to 340 times smaller than SPRA + `xz -9e` on these
  words. Decoding runs the construction (minutes and up to 2 GB at n = 13), and the reader must carry the pinned
  generator source.
* The two are different files with different magics (`SPRA`, `SPRC`). A container can embed the SPRA file, and then
  a reader without generators loses nothing.
* A word made by search (integer programming, local search) has no recipe; it stays SPRA only. For example, Jay
  Pantone's 6,747,918,066 word of the corpus has no published recipe.

## Writing containers

```sh
python3 tools/recipe/recipe.py selpack SEL OUT [m r]          # whole selection or (m, r) block solution
python3 tools/recipe/recipe.py transport SEL k OUT            # to make the base of a diff
python3 tools/recipe/recipe.py seldiff A B OUT [--rows-only]
python3 tools/recipe/recipe.py planenc PLAN OUT               # checks the round trip itself
python3 tools/recipe/recipe.py pack OUT.sprc --n N --alphabet A --length L --sha256 H \
        --steps STEPS --part NAME=FILE ... [--fallback FILE.spra.xz]
bash tools/recipe/make_examples.sh SUPERPERMUTATIONS PANTONE  # rebuilds the five examples (a few seconds)
```

`pack` takes the generators from the operations in the steps and writes the hashes of the local copies. It checks
the steps against the parts and refuses what a reader would refuse. It does not decode. Run
`recipe.py decode OUT.sprc` to check the length and hash.

`make_examples.sh` needs checkouts of github.com/jhalfar/superpermutations and
github.com/jaypantone/superperm-upper-43-80. It derives the 8-symbol selection from Pantone's
`tools/construction-input.txt` and takes the block solutions, the n = 11 connection files and the plans from
the first repository.

## Limits

* Family code: one generator per construction and version. New moves in later records need new plan line types or
  a new generator version.
* The generators here are Python. The rest of this repository is C++ and Bash. A C++ port of geng/applyplan would
  remove the Python dependency, and `gen14.c` already covers the selection-C words of n = 14.
* n = 8 and 9 words are rebuilt with an LP solver or by search, so they have no safe decoder and no container.

## Credits

The construction (2-loops, slices, selection, completion, transport, closed trails) is Jay Pantone's
(github.com/jaypantone/superperm-upper-43-80, Apache-2.0). The generators in `tools/recipe/gen/` are unchanged
copies from github.com/jhalfar/superpermutations (Apache-2.0); see `tools/recipe/NOTICE`.
