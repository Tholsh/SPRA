#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/spra-relabel-tests.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -pedantic "$root/tools/relabel_words.cpp" -o "$work/relabel.exe"
tool=$work/relabel.exe
printf '0 12\r\n010\t210\v\f' > "$work/source"
printf '1 23\r\n121\t321\v\f' > "$work/expected"
"$tool" --to 1 -n 3 "$work/source" "$work/output"
cmp "$work/expected" "$work/output"
"$tool" --to 0 -n 3 "$work/output" > "$work/restored"
cmp "$work/source" "$work/restored"
if "$tool" --to 1 "$work/source" "$work/output" 2>/dev/null; then exit 1; fi
printf invalid > "$work/bad"
if "$tool" --to 1 --overwrite "$work/bad" "$work/output" 2>/dev/null; then exit 1; fi
cmp "$work/expected" "$work/output"
if "$tool" --to 1 --overwrite "$work/source" "$work/source" 2>/dev/null; then exit 1; fi
ln "$work/source" "$work/hardlink"
if "$tool" --to 1 --overwrite "$work/source" "$work/hardlink" 2>/dev/null; then exit 1; fi
mkdir -p "$work/tree/sub"
cp "$work/source" "$work/tree/sub/a.TXT"
cp "$work/source" "$work/tree/ignored.bin"
"$tool" --to 1 -n 3 --recursive --dry-run "$work/tree" "$work/dry"
[[ ! -e $work/dry ]]
export RELABEL_TOOL=$tool
bash "$root/tools/relabel-folder.sh" --to 1 -n 3 "$work/tree" "$work/converted"
cmp "$work/expected" "$work/converted/sub/a.TXT"
[[ ! -e $work/converted/ignored.bin ]]
if "$tool" --to 1 --recursive "$work/tree" "$work/tree/nested" 2>/dev/null; then exit 1; fi
# POSIX symlinks are tested where the filesystem supports them.
if MSYS=winsymlinks:nativestrict ln -s "$work/output" "$work/symlink" 2>/dev/null; then
    if "$tool" --to 1 --overwrite "$work/source" "$work/symlink" 2>/dev/null; then exit 1; fi
fi
[[ -z $(find "$work" -name '.relabel-*' -print -quit) ]]
echo 'Relabel stream, binary whitespace, atomic output, folder and refusal checks passed'
