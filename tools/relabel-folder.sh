#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
# Usage: bash tools/relabel-folder.sh --to 0 [-n N] [--dry-run] SOURCE DESTINATION
set -euo pipefail
script_path="${BASH_SOURCE[0]//\\//}"
if [[ "$script_path" == */* ]]; then
    script_dir="$(cd -- "${script_path%/*}" && pwd)"
else
    script_dir="$PWD"
fi
if [[ -n "${RELABEL_PYTHON:-}" ]]; then
    python_command="$RELABEL_PYTHON"
elif command -v python3 >/dev/null 2>&1; then
    python_command=python3
else
    python_command=python
fi
exec "$python_command" "$script_dir/relabel_words.py" --recursive "$@"
