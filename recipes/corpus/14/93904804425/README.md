# Halfar N=14 tuple recipes

`n14_93904804425.kl.xz` is a [SPRA Kleene profile](../../../KLEENE.md) recipe,
using standard Kleene expression syntax and comment metadata. Its normalized
word uses one-based labels and the identity seed. The tuple sequence comes
from the corpus's Halfar word, relabeled bijectively to this convention.

```sh
make
bash recipes/kleene.sh info recipes/corpus/14/93904804425/n14_93904804425.kl.xz
set -o pipefail
bash recipes/kleene.sh emit recipes/corpus/14/93904804425/n14_93904804425.kl.xz |
  ./build/reduced_codec decode --strict | ./build/paint-waste -s 14
```

The recipe has 6,706,924,412 logical tuples and declares 93,904,804,425 decoded
symbols. Its xz size is 5,939,740 bytes. Its algebraic audit matches the source
tuple grammar; a full raw coverage run through the `.kl` runner has not yet
completed. Do not save or hash expanded N=14 files for routine validation.
The exact coverage checker needs about 10.2 GiB in addition to recipe memory.

The retired `halfar.kleene.xz` grammar is removed. No compatibility reader is
retained.

The superseded binary construction prototype is also removed from the
toolchain. Its previous validation does not serve as validation of this `.kl`
runner.

The superpermutation is Jakub Halfar's, using Jay Pantone's construction.
See the [corpus guide](../../../../docs/corpus.md) for upstream provenance.
