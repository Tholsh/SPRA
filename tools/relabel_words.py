#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
"""Relabel word streams or .txt files without changing whitespace or word length."""

import argparse
import os
from pathlib import Path
import sys
import tempfile

ZERO_BASED = b"0123456789ABC"
ONE_BASED = b"123456789ABCD"
WHITESPACE = b" \t\r\n\v\f"
CHUNK_SIZE = 1024 * 1024


def convert_stream(source, destination, target_base, n):
    """Validate and translate binary chunks, preserving every whitespace byte."""
    source_alphabet, target_alphabet = ((ONE_BASED, ZERO_BASED) if target_base == 0
                                        else (ZERO_BASED, ONE_BASED))
    allowed = source_alphabet[:n] + WHITESPACE
    translation = bytes.maketrans(source_alphabet, target_alphabet)
    offset = 0
    while True:
        chunk = source.read(CHUNK_SIZE)
        if not chunk:
            return
        if chunk.translate(None, allowed):
            for index, symbol in enumerate(chunk):
                if symbol not in allowed:
                    raise ValueError(f"invalid source symbol {bytes([symbol])!r} "
                                     f"at byte {offset + index}; "
                                     f"expected {source_alphabet[:n].decode()}")
        destination.write(chunk.translate(translation))
        offset += len(chunk)


def convert_file(source, destination, target_base, n, overwrite):
    """Publish a complete validated file; never edit the original in place."""
    if source.resolve() == destination.resolve() or (
            destination.exists() and os.path.samefile(source, destination)):
        raise ValueError("input and output must be different files")
    if destination.is_symlink():
        raise ValueError(f"refusing a symlink output: {destination}")
    if destination.exists() and not overwrite:
        raise FileExistsError(f"output already exists: {destination}; use --overwrite to replace it")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # The temporary file lives on the destination filesystem for atomic publish.
    descriptor, temporary_name = tempfile.mkstemp(prefix=".relabel-", dir=destination.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            with source.open("rb") as input_file:
                convert_stream(input_file, output, target_base, n)
        if overwrite:
            os.replace(temporary, destination)
        else:
            # An exclusive hard link prevents even a concurrent writer's file
            # from being overwritten after our initial existence check.
            os.link(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def folder_plan(source, destination, overwrite):
    """Preserve relative paths and keep output outside the input tree."""
    source, destination = source.resolve(), destination.resolve()
    if not source.is_dir():
        raise ValueError(f"input folder does not exist: {source}")
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("input and output folders must be separate, non-nested trees")
    plan = []
    for directory, subdirectories, filenames in os.walk(source, followlinks=False):
        subdirectories[:] = sorted(name for name in subdirectories
                                   if not (Path(directory) / name).is_symlink())
        for name in sorted(filenames):
            input_file = Path(directory) / name
            if input_file.suffix.lower() != ".txt" or input_file.is_symlink():
                continue
            output_file = destination / input_file.relative_to(source)
            # Resolve parents too: an existing symlink must not redirect output
            # into the source tree or anywhere outside the destination tree.
            if destination not in output_file.resolve().parents or output_file.is_symlink():
                raise ValueError(f"output escapes destination or is a symlink: {output_file}")
            if output_file.exists() and (not overwrite or not output_file.is_file()):
                raise FileExistsError(f"output already exists: {output_file}; "
                                      "use --overwrite to replace regular files")
            plan.append((input_file, output_file))
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        "--to 0 expects one-based input (123456789ABCD); "
        "--to 1 expects zero-based input (0123456789ABC). "
        "Uppercase letters are single symbols. Only word symbols and whitespace are accepted."))
    parser.add_argument("--to", type=int, choices=(0, 1), required=True, help="target indexing")
    parser.add_argument("-n", "--n", type=int, choices=range(1, 14), default=13,
                        metavar="N", help="source alphabet size, 1..13 (default: 13)")
    parser.add_argument("--recursive", action="store_true", help="convert all .txt files in a folder")
    parser.add_argument("--overwrite", action="store_true", help="replace existing output files")
    parser.add_argument("--dry-run", action="store_true", help="list folder conversions without writing")
    parser.add_argument("input", nargs="?", default="-", help="input file/folder, or - for stdin")
    parser.add_argument("output", nargs="?", default="-", help="output file/folder, or - for stdout")
    args = parser.parse_args()
    try:
        if args.recursive:
            if "-" in (args.input, args.output):
                parser.error("--recursive requires input and output folders")
            plan = folder_plan(Path(args.input), Path(args.output), args.overwrite)
            for source, destination in plan:
                if not args.dry_run:
                    convert_file(source, destination, args.to, args.n, args.overwrite)
                print(f"{source} -> {destination}", file=sys.stderr)
            print(f"{'Would convert' if args.dry_run else 'Converted'} {len(plan)} file(s).",
                  file=sys.stderr)
        else:
            if args.dry_run:
                parser.error("--dry-run requires --recursive")
            if args.input == "-" and args.output != "-":
                parser.error("stdin mode requires stdout; redirect stdout to save a streamed result")
            if args.input == "-":
                convert_stream(sys.stdin.buffer, sys.stdout.buffer, args.to, args.n)
            elif args.output == "-":
                with Path(args.input).open("rb") as source:
                    convert_stream(source, sys.stdout.buffer, args.to, args.n)
            else:
                convert_file(Path(args.input), Path(args.output), args.to, args.n, args.overwrite)
        return 0
    except (OSError, ValueError) as error:
        print(f"relabel: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
