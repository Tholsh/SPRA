# Changelog

Draft for review. Versions identify software releases; the SPRA wire format
remains 0.2. V0.1 predates this repository's recorded history; its entry is
limited to the legacy-format documentation and published comparisons.

## 0.2.1 — 2026-10-25

Tag: `v0.2.1` (`7e861e1`). Changes since `v0.2.0`:

- Integrated Jakub Halfar's codec fixes (PR #1): decode limits with
  `--max-symbols`, canonical decoding with `--strict`, and automatic six-bit fallback.
- Changed default xz compression to one thread, a 256 MiB dictionary, and
  `lc=0,lp=0,pb=0`; explicit xz arguments override these settings.
- Integrated Halfar's optional SPBE byte-pair layer (PR #2), with encode,
  decode, pack, and unpack commands. Recipe containers/generators were excluded.
- Made BPE streaming by default: bounded training, fixed pair rules, inline
  run definitions, and direct-run escapes; retained `--whole-file` mode.
- Added SPRA `measure`, header-first encoding with `--length`, `--tuples`,
  and `--bits`, and wrapper `encode-raw`. Wrappers measure first unless full
  metadata is supplied; stdin otherwise needs temporary storage.
- Corrected paint-waste tuples to decimal `[depth:signed-offset]` values
  matching the codec, with diagnostics for unrepresentable eight-bit fields.
- Added recursive folder decompression; standardized tool names and guides.
- Standardized execution on WSL/Linux Bash; added `make` and `make test`;
  removed PowerShell launchers, Windows executable discovery, and tracked editor/Git settings.
- Replaced the live-input test with Bash pipes/FIFOs and timeouts; expanded
  canonical, malformed-input, corpus, streaming, and byte-exact round-trip checks.

## 0.2.0 — 2026-10-21

Tag: `v0.2.0` (`b36ede2`), before Halfar's codec/BPE pull requests:

- Added mandatory tuple RLE before four-/six-bit packing; retained legacy
  `0x01` decoding, both label modes, and alphabet sizes N=1..32.
- Reduced the documented N=11 SPRA+xz `-6e` example from 13,540 to 8,012 bytes
  versus V0.1 (40.8%); field ranges and 64-bit length limits are unchanged.
- Distributed N=6 collections as tar+xz and N=9..14 words as one-based SPRA+xz,
  with unpacking/streaming tools and checksum manifests.
- Added Halfar's 93,904,804,425-symbol N=14 word as a 9,679,980-byte archive,
  with complete stream/hash verification.
- Provided paint-waste coverage statistics and bracket/color output, safe
  recursive relabeling, compression benchmarks, and independent verifiers.
- Replaced Python relabeling, benchmarks, and regression runners with C++/Bash.

## 0.1 — Historical; date/commit unavailable

- Original reduced-alphabet binary format, before tuple RLE.
- Retained as a decoding and compression-comparison baseline in V0.2.
