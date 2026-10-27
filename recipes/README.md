# Kleene tuple recipes

Recipes are Kleene `.kl` source compressed with xz. The
[SPRA Kleene profile](KLEENE.md) defines tuple symbols and three metadata
comments. Every recipe uses one-based labels and the identity seed. Tuple
count and canonical SPRA field width are derived from its expressions.

From Linux/WSL Bash at the repository root:

```sh
make
bash recipes/kleene.sh info recipes/corpus/14/93904804425/n14_93904804425.kl.xz
bash recipes/kleene.sh audit recipes/corpus/14/93904804425/n14_93904804425.kl.xz

# The one streaming entry point for every .kl recipe.
set -o pipefail
bash recipes/kleene.sh emit recipes/corpus/14/93904804425/n14_93904804425.kl.xz |
  ./build/reduced_codec decode --strict | ./build/paint-waste -s 14
```

`emit` accepts `.kl`, `.kl.xz`, or decompressed source on stdin. It checks
metadata and expressions before output, then enforces tuple semantics and
available-copy references while streaming. The wrapper writes canonical SPRA
RLE and packing. Invalid execution can fail after partial output; discard
that output and check pipeline status. Coverage remains the raw checker's job.

The runner evaluates a finite singleton-expression subset of Kleene, keeping
shared expressions and counted repetitions compact in memory. It does not
implement all of Kleene's programming language or build a giant explicit FSA.
There is no reader for the retired `KLEENE-SPRA` format.

To mine a recipe from an existing SPRA stream:

```sh
bash tools/spra-xz.sh encode 5 words/5/153/153-recursive.txt > build/n5.spra.xz
bash recipes/kleene.sh compile build/n5.spra.xz | xz -c -T1 -6 > build/n5_153.kl.xz
bash recipes/kleene.sh emit build/n5_153.kl.xz |
  ./build/reduced_codec decode --strict | ./build/paint-waste -s 5
```

The importer normalizes a distinct source seed to identity by relabeling;
tuple depths and offsets are unchanged. A repeated-symbol source seed cannot
be normalized this way and is refused. Other gauges are not stored in recipes.

Large recipes can explicitly raise [resource limits](KLEENE.md#execution-and-canonical-output):

```sh
bash recipes/kleene.sh audit --max-nodes 3000000 /path/to/n15_1401325415364.kl.xz
```

Use the same options with `emit` to enumerate the complete SPRA stream.
`--max-edges`, `--max-source-bytes` and `--max-nesting` are also available.
These settings raise loading budgets; they do not limit the output to a prefix.

```sh
make -C recipes test
bash recipes/benchmark.sh 5 build/n5.spra.xz build/recipes/benchmark-n5 --paint
```

Benchmarks require GNU time, jq and xz. They measure mining, xz compression,
audit/loading and SPRA emission to `/dev/null`. `--paint` also compares raw
checker reports byte for byte, checks coverage and requires the declared
length. No expanded files or hashes are used for N>=13. The N=14 checker needs
about 10.2 GiB in addition to recipe execution memory.

`audit` derives counts, run boundaries and wire size without expanding the
tuple sequence; it does not establish semantic correctness or coverage.
The current N=14 `.kl.xz` import is **5,939,740 bytes**. Its algebraic audit
matches the source representation, but a full raw coverage run of this `.kl`
runner has not been completed. Small corpus recipes pass full strict decoding
and raw coverage checks in the regression suite.

The superseded binary construction prototype and legacy grammar are removed
from this toolchain. Capturing the construction compactly in this Kleene
profile remains open work. The streamer does not check whether a valid copy
reference is the nearest one; use `decode --strict` for that conformance check.
