# Corpus archives: unpack or stream

Four large N=6 collections are stored as folder archives in `words/6/872/`:

| Archive | Words | Original bytes | Archive bytes |
| --- | ---: | ---: | ---: |
| `872-nonstandard.tar.xz` | 1,024 | 894,976 | 36,740 |
| `872-tahg.tar.xz` | 4,208 | 3,677,792 | 154,852 |
| `872-treelike.tar.xz` | 42,288 | 36,959,712 | 1,251,964 |
| `872-treelike-slack1.tar.xz` | 772 | 674,728 | 22,564 |

Together, 48,292 files shrink from **42,207,208 bytes to 1,466,120 bytes**
(96.53% smaller). Archives preserve the exact original one-based text files,
including line endings, inside a folder with the collection's name. No
individual SPRA encoding is used for these collections.

`words/6/872/1/` is untouched and contains four uncompressed examples. The
1,681 words previously loose in `words/6/872/` now live in
`words/6/872/base/`, also uncompressed. Smaller N=6 collections and the N=5,
N=7, and N=8 files retain their existing formats and labels.

N=9 through N=13 use one-based `.spra.xz` words at
`words/N/LENGTH/NAME.spra.xz`: SPRA 0.2 followed by `xz -6e`.
Four-bit SPRA fields are tried first; words exceeding their range use six-bit
fields. Decode detects the width and writes normalized symbols without
whitespace or a final newline.

[`collection-archives.json`](../words/6/872/collection-archives.json) records the
N=6 sizes, counts, checksums, and verification results.
[`spra-summary.json`](../words/spra-summary.json) and
`words/spra-manifest.jsonl.xz` cover the N>=9 SPRA corpus separately.

## Build the SPRA decoder

The shell scripts need Bash, tar, and xz. SPRA also needs the C++20 decoder.
From the repository root:

```sh
mkdir -p build
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec
```

With MinGW GCC in PowerShell:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++20 -O2 tools/reduced_codec.cpp -o build/reduced-codec.exe
```

MSYS2 or Git Bash supplies the shell tools on Windows. Set `SPRA_CODEC` to use
a different decoder executable. Only tar and xz are needed for N=6 collections.

## Unpack a collection or an alphabet

To restore one collection beside its archive:

```sh
tar -xJf words/6/872/872-treelike.tar.xz -C words/6/872
# Creates words/6/872/872-treelike/872-00001.txt, etc.
```

The expanded large collection folders are ignored by git. The `1/` and `base/`
folders remain eligible for version control.

The [Bash script](unpack-corpus.sh) and [PowerShell launcher](unpack-corpus.ps1)
create a complete working copy of an alphabet, combining expanded archives and
existing raw words. They preserve all source files and archives, copy existing
`.txt` files, decompress `.txt.xz` files without relabeling, and decode
`.spra.xz` files through the wrapper. The destination must not exist and must be
outside the source `words/` tree. Failed runs remove temporary output.

```sh
# Creates build/unpacked-n6/6/872/ with all collections, 1/, and base/.
bash docs/unpack-corpus.sh 6 build/unpacked-n6

# All alphabets; default output is the ignored build/unpacked/ tree.
bash docs/unpack-corpus.sh
bash docs/unpack-corpus.sh all build/unpacked-all
```

```powershell
./docs/unpack-corpus.ps1 -N 6 -Destination build/unpacked-n6
./docs/unpack-corpus.ps1
./docs/unpack-corpus.ps1 -N 9 -Destination build/unpacked-n9 -Bash C:/msys64/usr/bin/bash.exe
```

The complete materialized corpus is about 38.9 GB of symbols, plus filesystem
overhead. Choose an alphabet or stream a single word to use less disk space.

## Stream a word without extracting files

For an N=6 collection, list members and select one file with tar's `-O`:

```sh
tar -tJf words/6/872/872-treelike.tar.xz
tar -xOJf words/6/872/872-treelike.tar.xz 872-treelike/872-00001.txt

# Analyze the original text, or save it only when wanted.
set -o pipefail
tar -xOJf words/6/872/872-treelike.tar.xz 872-treelike/872-00001.txt | ./build/paint-waste -s 6
tar -xOJf words/6/872/872-treelike.tar.xz 872-treelike/872-00001.txt > build/raw-n6.txt
```

For N>=9, the [SPRA+xz wrapper](../tools/spra-xz.sh) writes **only raw word
symbols to stdout**, without a final newline. It performs both xz decompression
and SPRA decoding; `xz -dc` alone produces binary SPRA bytes.

```sh
# Raw stdout; no word file is created.
bash tools/spra-xz.sh decode words/9/408731/superpermutation-9-408731.spra.xz

set -o pipefail
bash tools/spra-xz.sh decode words/9/408731/superpermutation-9-408731.spra.xz | ./build/paint-waste -s 9
bash tools/spra-xz.sh decode words/9/408731/superpermutation-9-408731.spra.xz > build/raw-n9.txt

# The wrapper also accepts an archive through stdin.
cat words/9/408731/superpermutation-9-408731.spra.xz | bash tools/spra-xz.sh decode -
```

On PowerShell, let Bash handle redirection for the wrapper, or use a native
binary pipeline through `cmd /c`:

```powershell
bash -c 'bash tools/spra-xz.sh decode words/9/408731/superpermutation-9-408731.spra.xz > build/raw-n9.txt'
cmd /c 'xz -dc words\9\408731\superpermutation-9-408731.spra.xz | .\build\reduced-codec.exe decode > build\raw-n9.txt'
```

Check the pipeline's exit status before using redirected output. See the
[codec guide](reduced_binary.md) for encoding options and format limits.
