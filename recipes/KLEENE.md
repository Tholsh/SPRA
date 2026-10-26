# SPRA Kleene profile, version 1

This profile specifies a recipe for one finite sequence of SPRA logical tuples
using syntax from Kenneth Beesley's Kleene finite-state programming language.
It is an application profile of Kleene, not a claim to implement the full
language. The current runner supports precisely the finite expressions below.

## Source and metadata

Use `.kl` for source and `.kl.xz` for xz-compressed source. A descriptive name
such as `n8_46203.kl.xz` is recommended. The source comments are authoritative;
renaming a recipe does not change its meaning. This also permits stdin use.

Exactly these three metadata declarations must occur before all statements,
in ordinary Kleene line comments, in any order:

```kl
// @spra version 1
// @spra n 3
// @spra length 9

$word = '[3,+1]';
```

This complete example decodes to `123121321`. `version` identifies this recipe
profile, not the SPRA wire version. `n` is an integer in 1..32. `length` is the
decoded symbol count as an unsigned 64-bit integer. Missing, duplicate, unknown
or late `@spra` declarations are errors. Other comments are ignored. Ordinary
`//` and nested `/* ... */` comments are supported.

All recipes use the one-based alphabet `123456789ABCDEFGHIJKLMNOPQRSTUVW`.
The seed is the first `min(length,n-1)` identity IDs `0,1,...`, corresponding to
the initial symbols `123...`. The profile has no seed or label override.
This fixes the initial label gauge; it does not choose between reversal or
other constructions with the same N and length.

## Expressions

| Syntax | Meaning |
| --- | --- |
| `'[d,o]'` | One tuple alphabet symbol: clean depth d and signed copy offset o |
| `$name` | A previously defined network expression |
| `A B` | Concatenation, in order |
| `(A)` | Grouping |
| `A{k}` | Exactly k copies, including k=0 |
| `_e_` | The empty tuple sequence, Kleene's ASCII epsilon |
| `$name = A;` | Define an expression |

Names use ASCII letters and underscores followed by letters, underscores or
digits, prefixed by `$`. Definitions occur once, refer only to earlier
definitions and end in semicolons. The final definition must be `$word`.
Only expressions reachable from `$word` contribute to output or field width.
No external recipe or construction engine is selected by metadata.

A tuple such as `'[3,+1]'` is **one multi-character Kleene symbol**, not a
sequence of printed punctuation characters. Unsigned depths are 0..63;
offsets are -32..31 and must be less than n. An optional `+` is allowed on
nonnegative offsets. Positive depth with zero offset is not a logical tuple:
SPRA reserves that physical token shape for its existing RLE controls.

Counts in Kleene expressions are nonnegative signed-64-bit integer literals.
Nested expressions can describe larger sequences, provided resulting tuple
counts and clean-step totals fit unsigned 64-bit integers. Counts are checked
for overflow. Tuple-count arithmetic and maximum field width are evaluated
without expanding repetitions.

Alternation, unbounded repetition, variable reassignment, forward/recursive
references, functions, imports, transducers and other Kleene features are
outside this version's runner. They fail explicitly. This restriction gives
each accepted expression exactly one finite output sequence. Equivalent
programs need not have identical source text: there is no canonical spelling
or minimization requirement for the source.

## Execution and canonical output

`bash recipes/kleene.sh emit FILE.kl.xz` decompresses and checks the source,
constructs the SPRA 0.2 header and identity seed, and executes `$word` lazily.
It never materializes the complete tuple sequence, SPRA file or raw word.
Source expressions are retained in memory; a traversal stack executes them.

For every tuple, the runtime generates d clean steps internally, then copies
at distance `n-o`. It checks available history,
and that the copied symbol creates a dirty window. After the last tuple,
the declared length determines the remaining clean steps; they must be valid.
These are the [SPRA logical tuple semantics](../docs/spra.md#logical-tuples-and-repeat-markers).
Only bounded symbol history is needed, not a coverage bitmap.

Output is canonical SPRA: four-bit fields exactly when all reachable tuples
fit them, otherwise six-bit fields; maximal identical-tuple runs use SPRA's
existing canonical RLE; bits are packed MSB first with zero final padding.
No new RLE format is introduced. Repetition in the Kleene source can repeat
arbitrary multi-tuple expressions, independently of the wire RLE.

A successful `emit` guarantees this prescribed gauge, valid tuple execution,
canonical field selection/RLE/packing and the declared length. It does not
check that valid copy references use the nearest occurrence; recipes are
required to use nearest references, and `decode --strict` checks that rule
independently. Successful streaming does **not** establish
superpermutation coverage; use strict decoding into the raw-stream checker.
Late errors, including corrupt xz input, may follow partial output. Nonzero
pipeline status invalidates that output. Diagnostics go to stderr.

The current implementation caps source at 256 MiB, expressions at two million
nodes, child references at 32 million, and expression/comment nesting at 256.
These are runner resource limits. A recipe outside them is refused rather
than silently interpreted differently.

## Provenance and conformance

Syntax was checked against the upstream Kleene grammar and manual at commit
`938beb074bcf3706852630881da15e5badb730d5`:

- [Grammar](https://github.com/krbeesley/kleene-lang/blob/938beb074bcf3706852630881da15e5badb730d5/kleene/src/main/jjtree/org/kleene-lang/Kleene.jjt)
- [Symbols and operators](https://github.com/krbeesley/kleene-lang/blob/938beb074bcf3706852630881da15e5badb730d5/doc/user/kleene/chapt3.tex)

The native Kleene/OpenFst runtime has not been built or used for differential
testing here. The local runner preserves the semantics of the supported
expressions without constructing their potentially enormous explicit FSA.

`make -C recipes test` includes independent corpus words at N=5,7,8, canonical
byte comparisons, strict decoding, full raw coverage for those fixtures,
source gauge normalization, comment metadata, tuple boundaries, counted
repetitions, overflow and malformed/unsupported source refusal. A `.kl` recipe
does not inherit another representation's benchmark or coverage result.
