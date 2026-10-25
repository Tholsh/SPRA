# Streaming BPE benchmark

Measured on AMD Ryzen 5 9600X, Windows, native xz 5.4.3. All 36 existing N=9..14 SPRA archives were processed with default SPBE 2 (65,536 training runs, minimum pair count 16). Each result was decoded and its complete SPRA SHA-256 compared with a fresh decompression of its source. Every comparison matched. No corpus files were replaced.

Both size columns use `-T1 --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0`. Plain-xz sizes reuse the earlier verified baseline on these same files/settings. Streaming timings are one full pass and include source xz decompression, BPE encoding, process startup and concurrent xz compression; validation is excluded. Small timing differences are noise.

| N | Files | Plain SPRA+xz bytes | Streaming BPE+xz bytes | Reduction | Encode seconds | BPE peak MiB | Pipeline peak sum MiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 9 | 7 | 2472 | 2344 | 5.2% | 0.46 | 5.32 | 17.2 |
| 10 | 6 | 12312 | 10092 | 18.0% | 0.46 | 5.88 | 25.2 |
| 11 | 8 | 96708 | 48832 | 49.5% | 0.75 | 5.87 | 36.2 |
| 12 | 9 | 771344 | 303060 | 60.7% | 3.05 | 5.88 | 69.7 |
| 13 | 5 | 2516184 | 951728 | 62.2% | 19.83 | 5.88 | 393.6 |
| 14 | 1 | 6143360 | 1236720 | 79.9% | 31.13 | 5.87 | 1001.4 |

Memory uses native process PeakWorkingSet64 sampled every 1 ms. BPE peak is the BPE encoder process alone. Pipeline peak sum adds each stage peak and is an upper bound, not simultaneous aggregate memory. xz remains the largest memory user. The BPE stage stays near 6 MiB instead of retaining the whole run stream. Raw runs and SPRA hashes are in `build/benchmarks/bpe-stream/runs.csv`.

For N=12 word 522,736,969, a separate same-settings comparison gave 99,680 bytes for plain xz, 38,128 for Halfar original whole-file BPE, and 39,052 for streaming BPE. Both BPE paths restored the original SPRA bytes exactly.

Streaming trades some global optimization for bounded memory and early output. Halfar reports whole-file totals of 559,676 bytes for N=13 and 859,588 for N=14 using xz 5.8.2; these are smaller than this streaming version but retain the complete run stream during encoding (reported N=14 encoder peak 3.1 GB). Use `--whole-file` to select that compression/memory tradeoff.

Validation also includes the reference-format checks, 1,201 independent online/offline pair-substitution checks, streaming truncations and invalid records, more than 256 distinct run types, split long runs, strict/length-limit forwarding, raw-word hashes, and live input tests proving payload appears before EOF on encoding, decoding, and the complete `.bpe.xz` to raw-word pipeline. SPRA encoder spooling is outside this change.
