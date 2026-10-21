# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.

"""Compare SPRA layouts and xz settings, optionally against an older codec."""
import argparse
import hashlib
import json
import lzma
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CHUNK = 1024 * 1024
WHITESPACE = b" \t\r\n\f\v"


def digest_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_xz(xz, source, destination, environment, decompress=False, arguments=()):
    command = [xz, "-d"] if decompress else [xz, *arguments]
    with source.open("rb") as input_file, destination.open("wb") as output_file:
        result = subprocess.run(command, stdin=input_file, stdout=output_file,
                                stderr=subprocess.PIPE, env=environment)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace").strip())


def verify_file(path, expected_size, expected_hash):
    if path.stat().st_size != expected_size or digest_file(path) != expected_hash:
        raise RuntimeError(f"round trip differs from input: {path.name}")


def positive_int(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def measure_layouts(codec, bits_list, n, zero_based, word, work, xz, environment,
                    xz_arguments, expected_size, expected_hash, prefix):
    layouts = []
    for bits in bits_list:
        packed = work / f"{prefix}-{bits}.spra"
        command = [str(codec), "encode", "--bits", str(bits)]
        if zero_based:
            command.append("-z")
        with packed.open("wb") as output:
            encoded = subprocess.run(command + [str(n), str(word)],
                                     stdout=output, stderr=subprocess.PIPE)
        if encoded.returncode:
            layouts.append({"bits_per_field": bits, "fits": False,
                            "error": encoded.stderr.decode(errors="replace").strip()})
            continue
        with packed.open("rb") as source:
            header = source.read(24)
        if len(header) != 24 or header[:4] != b"SPRA":
            raise RuntimeError("codec did not produce a SPRA header")
        logical_tuples = int.from_bytes(header[16:24], "little")
        body_bytes = packed.stat().st_size - 24 - min(expected_size, n - 1)
        archive = work / f"{prefix}-{bits}.spra.xz"
        unpacked = work / f"{prefix}-unpacked.spra"
        run_xz(xz, packed, archive, environment, arguments=xz_arguments)
        run_xz(xz, archive, unpacked, environment, decompress=True)
        verify_file(unpacked, packed.stat().st_size, digest_file(packed))
        restored = work / f"{prefix}-restored.txt"
        with restored.open("wb") as output:
            decoded = subprocess.run([str(codec), "decode", str(unpacked)],
                                     stdout=output, stderr=subprocess.PIPE)
        if decoded.returncode:
            raise RuntimeError(decoded.stderr.decode(errors="replace").strip())
        verify_file(restored, expected_size, expected_hash)
        layouts.append({"bits_per_field": bits, "fits": True,
                        "spra_version_byte": header[4], "logical_tuples": logical_tuples,
                        "stored_tokens": body_bytes * 8 // (2 * bits),
                        "spra_bytes": packed.stat().st_size,
                        "xz_bytes": archive.stat().st_size,
                        "round_trip_sha256_matches": True})
    return layouts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("n", type=int, choices=range(1, 33), metavar="N")
    parser.add_argument("input", type=Path, help="word file, optionally .xz compressed")
    executable = "reduced-codec.exe" if os.name == "nt" else "reduced-codec"
    default_codec = ROOT / "build" / executable
    if not default_codec.exists():
        default_codec = ROOT / executable
    parser.add_argument("--codec", type=Path, default=default_codec)
    parser.add_argument("--baseline-codec", type=Path,
                        help="also measure this codec using identical xz settings")
    parser.add_argument("--xz-arg", action="append", default=[], metavar="OPTION",
                        help="xz compression option, repeatable; e.g. --xz-arg=-6e")
    parser.add_argument("-z", "--zero-based", action="store_true")
    parser.add_argument("--bits", type=int, choices=[4, 6], nargs="+", default=[4, 6])
    parser.add_argument("--prefix-bytes", type=positive_int,
                        help="test only this many decompressed input bytes, before whitespace removal")
    args = parser.parse_args()
    codec = str(args.codec.resolve())
    xz = shutil.which("xz")
    if xz is None:
        raise RuntimeError("xz is required on PATH")
    environment = os.environ.copy()
    for name in ("XZ_DEFAULTS", "XZ_OPT"):
        environment.pop(name, None)
    version = subprocess.run([xz, "--version"], capture_output=True, check=True,
                             env=environment).stdout.decode().splitlines()[0]
    with tempfile.TemporaryDirectory(prefix="reduced-codec-benchmark-") as directory:
        work = Path(directory)
        word = work / "word.txt"
        opener = lzma.open if args.input.suffix.lower() == ".xz" else open
        with opener(args.input, "rb") as source, word.open("wb") as output:
            remaining = args.prefix_bytes
            while remaining is None or remaining > 0:
                chunk = source.read(CHUNK if remaining is None else min(CHUNK, remaining))
                if not chunk:
                    break
                if remaining is not None:
                    remaining -= len(chunk)
                output.write(chunk.translate(None, WHITESPACE))
        expected_hash = digest_file(word)
        word_size = word.stat().st_size
        direct_archive = work / "word.txt.xz"
        direct_restored = work / "direct-restored.txt"
        run_xz(xz, word, direct_archive, environment, arguments=args.xz_arg)
        run_xz(xz, direct_archive, direct_restored, environment, decompress=True)
        verify_file(direct_restored, word_size, expected_hash)
        result = {
            "input": str(args.input), "n": args.n, "zero_based": args.zero_based,
            "prefix_bytes_limit": args.prefix_bytes,
            "word_bytes": word_size, "word_sha256": expected_hash,
            "compressor": "xz", "settings": "explicit options" if args.xz_arg else "defaults",
            "compression_arguments": args.xz_arg, "xz_version": version,
            "python": platform.python_version(),
            "direct_xz_bytes": direct_archive.stat().st_size,
            "direct_round_trip_sha256_matches": True, "layouts": [],
        }
        result["layouts"] = measure_layouts(codec, args.bits, args.n, args.zero_based,
            word, work, xz, environment, args.xz_arg, word_size, expected_hash, "current")
        if args.baseline_codec:
            result["baseline_layouts"] = measure_layouts(args.baseline_codec.resolve(),
                args.bits, args.n, args.zero_based, word, work, xz, environment,
                args.xz_arg, word_size, expected_hash, "baseline")
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, lzma.LZMAError, RuntimeError) as error:
        print(f"benchmark-reduced-codec: {error}", file=sys.stderr)
        sys.exit(1)
