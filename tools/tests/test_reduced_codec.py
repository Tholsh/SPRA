# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.

"""Exercise the public binary format, stdout behavior, and lossless reconstruction."""
import os
import json
import lzma
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ONE_BASED = b"123456789ABCDEFGHIJKLMNOPQRSTUVW"
ZERO_BASED = b"0123456789ABCDEFGHIJKLMNOPQRSTUV"


def packet(n, length, seed, tuples, flags=0, bits=4, version=2, logical_count=None):
    bit_string = "".join(format(value, f"0{2 * bits}b") for value in tuples)
    bit_string += "0" * (-len(bit_string) % 8)
    body = bytes(int(bit_string[i:i+8], 2) for i in range(0, len(bit_string), 8))
    return (b"SPRA" + bytes([version, (bits << 4) | bits, flags, n])
            + struct.pack("<QQ", length, len(tuples) if logical_count is None else logical_count)
            + bytes(seed) + body)


class ReducedCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.xz = shutil.which("xz")
        if cls.xz is None:
            raise RuntimeError("xz is required on PATH")
        cls.xz_environment = os.environ.copy()
        for name in ("XZ_DEFAULTS", "XZ_OPT"):
            cls.xz_environment.pop(name, None)
        cls.directory = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.directory.name) / "codec with spaces.exe"
        subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra",
                        "-pedantic", str(ROOT / "tools/reduced_codec.cpp"),
                        "-o", str(cls.binary)], check=True)
        compiler = Path(shutil.which("g++"))
        bash_candidates = [os.environ.get("SPRA_TEST_BASH"),
                           compiler.parents[2] / "usr/bin/bash.exe",
                           Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe",
                           shutil.which("bash")]
        cls.bash = next((str(path) for path in bash_candidates if path and Path(path).is_file()), None)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_codec(self, data, *args):
        return subprocess.run([str(self.binary), *map(str, args)], input=data,
                              capture_output=True, timeout=30)

    def encode(self, word, n, *flags):
        result = self.run_codec(word, "encode", *flags, n)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        return result.stdout

    def decode(self, data):
        result = self.run_codec(data, "decode")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        return result.stdout

    def xz_round_trip(self, data):
        compressed = subprocess.run([self.xz], input=data, capture_output=True,
                                    env=self.xz_environment, timeout=30)
        self.assertEqual(compressed.returncode, 0, compressed.stderr)
        self.assertTrue(compressed.stdout.startswith(b"\xfd7zXZ\x00"))
        restored = subprocess.run([self.xz, "-d"], input=compressed.stdout,
                                 capture_output=True, env=self.xz_environment, timeout=30)
        self.assertEqual(restored.returncode, 0, restored.stderr)
        self.assertEqual(restored.stdout, data)
        return restored.stdout

    def test_hand_written_format_vector(self):
        # Seed 12; three clean windows; copy two symbols back (3-2 = +1);
        # the remaining three symbols follow cleanly from decoded length 9.
        expected = packet(3, 9, [0, 1], [0x31])
        self.assertEqual(self.encode(b"123121321", 3), expected)
        self.assertEqual(self.decode(expected), b"123121321")
        self.assertEqual(len(expected), 24 + 2 + 1)

    def test_whitespace_and_zero_based_labels(self):
        binary = self.encode(b"0 12\r\n010\t210\v\f", 3, "-z")
        self.assertEqual(binary, packet(3, 9, [0, 1], [0x31], flags=1))
        self.assertEqual(self.decode(binary), b"012010210")
        self.assertEqual(self.encode(b"012010210", 3, "--zero-based"), binary)

    def test_six_bit_vectors_odd_even_and_zero_offset(self):
        for word, tuples, body in [
            (b"123121321", [0x0C1], b"\x0c\x10"),
            (b"1231213211", [0x0C1, 0x0C2], b"\x0c\x10\xc2"),
            (b"21312", [2, 0], b"\x00\x20\x00"),
        ]:
            n = 4 if word == b"21312" else 3
            seed = [1, 0, 2] if n == 4 else [0, 1]
            expected = packet(n, len(word), seed, tuples, bits=6)
            self.assertEqual(expected[24 + n - 1:], body)
            self.assertEqual(self.encode(word, n, "--bits", 6), expected)
            self.assertEqual(self.decode(expected), word)
        # Selecting the default width explicitly preserves the 0.2 encoding.
        self.assertEqual(self.encode(b"123121321", 3, "--bits=4"),
                         self.encode(b"123121321", 3))
        self.assertEqual(self.encode(b"012010210", 3, "--bits=6", "-z"),
                         packet(3, 9, [0, 1], [0x0C1], flags=1, bits=6))

    def test_repeat_previous_vectors_and_legacy_decode(self):
        for bits in [4, 6]:
            with self.subTest(bits=bits):
                # Seed 11; one dirty literal (0,+2), then two additional copies.
                marker = 2 << bits
                encoded = packet(3, 5, [0, 0], [2, marker], bits=bits, logical_count=3)
                self.assertEqual(self.encode(b"11111", 3, "--bits", bits), encoded)
                self.assertEqual(self.decode(encoded), b"11111")
                # Zero remains a literal and can itself be repeated.
                zero = packet(4, 7, [1, 0, 2], [2, 0, marker], bits=bits, logical_count=4)
                word = self.decode(zero)
                self.assertEqual(word, b"2131213")
                # Decode also accepts non-nearest references; canonical encode
                # may choose a different tuple sequence for this word.
                self.assertEqual(self.decode(self.encode(word, 4, "--bits", bits)), word)
                legacy = packet(3, 5, [0, 0], [2, 2, 2], bits=bits, version=1)
                self.assertEqual(self.decode(legacy), b"11111")
                self.assertEqual(self.decode(packet(3, 9, [0, 1],
                    [(3 << bits) | 1], bits=bits, version=1)), b"123121321")

    def test_repeat_count_boundaries_and_chains(self):
        for bits in [4, 6]:
            limit = (1 << bits) - 2
            for repeats in [0, 1, limit - 1, limit, limit + 1, 2 * limit, 2 * limit + 1]:
                with self.subTest(bits=bits, repeats=repeats):
                    markers = [(limit << bits)] * (repeats // limit)
                    if repeats % limit:
                        markers.append((repeats % limit) << bits)
                    expected = packet(3, repeats + 3, [0, 0], [2, *markers],
                                      bits=bits, logical_count=repeats + 1)
                    word = b"1" * (repeats + 3)
                    self.assertEqual(self.encode(word, 3, "--bits", bits), expected)
                    self.assertEqual(self.decode(expected), word)

    def test_repeat_marker_validation(self):
        for bits in [4, 6]:
            reserved = ((1 << bits) - 1) << bits
            for data, message in [
                (packet(3, 4, [0, 0], [1 << bits], bits=bits), b"no preceding tuple"),
                (packet(3, 4, [0, 0], [reserved], bits=bits), b"reserved tuple extension"),
                (packet(3, 8, [0, 0], [2, 2 << bits], bits=bits, logical_count=2),
                 b"exceeds logical tuple count"),
                (packet(3, 4, [0, 0], [2, 2 << bits], bits=bits, logical_count=3),
                 b"exceeds decoded length"),
                (packet(3, 6, [0, 0], [2, 1 << bits], bits=bits, logical_count=4),
                 b"truncated tuple body"),
                # Re-execute against updated state: a second (1,+2) cannot
                # generate a clean step after its first dirty copy.
                (packet(3, 6, [0, 1], [(1 << bits) | 2, 1 << bits],
                        bits=bits, logical_count=2), b"repeated suffix"),
            ]:
                with self.subTest(bits=bits, message=message):
                    result = self.run_codec(data, "decode")
                    self.assertEqual(result.returncode, 1)
                    self.assertIn(message, result.stderr)
            valid = packet(3, 5, [0, 0], [2, 2 << bits], bits=bits, logical_count=3)
            self.assertEqual(self.decode(valid), b"11111")
            self.assertNotEqual(self.run_codec(valid + b"\x00", "decode").returncode, 0)
            if bits == 6:
                # Three physical tokens, five logical tuples: final half-byte
                # padding belongs to the physical token stream.
                odd = packet(3, 7, [0, 0], [2, 2 << bits, 2 << bits],
                             bits=bits, logical_count=5)
                self.assertEqual(self.decode(odd), b"1111111")
                result = self.run_codec(odd[:-1] + bytes([odd[-1] | 1]), "decode")
                self.assertEqual(result.returncode, 1)
                self.assertIn(b"padding", result.stderr)

    def test_empty_short_repeated_and_full_alphabets(self):
        for n in [1, 2, 3, 8, 13, 16, 17, 32]:
            for alphabet, flags in [(ONE_BASED, []), (ZERO_BASED, ["-z"])]:
                for word in [b"", alphabet[:max(0, n-2)], alphabet[:n],
                             alphabet[:n] * 3]:
                    for bits in [4, 6]:
                        with self.subTest(n=n, word=word, flags=flags, bits=bits):
                            self.assertEqual(self.decode(self.encode(word, n, *flags, "--bits", bits)), word)
        self.assertEqual(self.decode(self.encode(b"1111", 3)), b"1111")
        self.assertEqual(self.decode(self.encode(b"1" * 1000, 1)), b"1" * 1000)

    def test_n16_with_a_dirty_tuple(self):
        word = ONE_BASED[:16] + b"2"
        encoded = self.encode(word, 16)
        self.assertEqual(encoded, packet(16, 17, range(15), [0x11]))
        self.assertEqual(self.decode(encoded), word)

    def test_signed_boundaries_and_zero_offset(self):
        positive = b"123456788"  # +7: copy the immediately preceding 8.
        self.assertEqual(self.encode(positive, 8)[-1], 0x17)
        self.assertEqual(self.decode(self.encode(positive, 8)), positive)
        negative = b"12" + b"1" * 10 + b"2"  # -8: distance 11 at N=3.
        self.assertEqual(self.encode(negative, 3)[-1], 0x08)
        self.assertEqual(self.decode(self.encode(negative, 3)), negative)
        zero = b"21312"  # Distance-N copying can still make a dirty window.
        self.assertEqual(self.encode(zero, 4)[-2:], b"\x02\x00")
        self.assertEqual(self.decode(self.encode(zero, 4)), zero)

    def test_six_bit_signed_depth_and_history_boundaries(self):
        positive = ONE_BASED + ONE_BASED[-1:]  # N=32, distance 1, offset +31.
        self.assertEqual(self.encode(positive, 32, "--bits", 6)[-2:], b"\x05\xf0")
        negative = b"12" + b"1" * 34 + b"2"  # N=3, distance 35, offset -32.
        longest = ONE_BASED + ONE_BASED[-1:] * 32 + b"1"  # N=32, distance 64.
        self.assertEqual(self.encode(negative, 3, "--bits", 6),
                         packet(3, len(negative), [0, 1], [1, 2, 32 << 6, 32],
                                bits=6, logical_count=35))
        self.assertEqual(self.encode(longest, 32, "--bits", 6),
                         packet(32, len(longest), range(31), [95, 31, 30 << 6, 32],
                                bits=6, logical_count=33))
        depth = b"12" * 32 + b"2"  # 63 clean windows, then dirty.
        self.assertEqual(self.encode(depth, 2, "--bits", 6)[-2:], b"\xfc\x10")
        for n, word in [(32, positive), (3, negative), (32, longest), (2, depth)]:
            self.assertEqual(self.decode(self.encode(word, n, "--bits", 6)), word)
        for n, word, message in [
            (3, b"12" + b"1" * 35 + b"2", b"copy distance 36"),
            (2, b"12" * 32 + b"11", b"depth 64"),
            (3, b"112", b"no earlier occurrence"),
        ]:
            result = self.run_codec(word, "encode", "--bits", 6, n)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, b"")
            self.assertIn(message, result.stderr)

    def test_former_bounded_history_collision_is_distinguished(self):
        prefix = b"123121321" + b"1" * 7
        words = [prefix + symbol + b"1" * 7 for symbol in [b"2", b"3"]]
        binaries = [self.encode(word, 3) for word in words]
        self.assertNotEqual(*binaries)
        self.assertEqual([self.decode(data) for data in binaries], words)
        # One payload includes LF as a binary tuple, not a text line ending.
        self.assertIn(b"\x0a", binaries[0][26:])

    def test_depth_boundary_and_unbounded_trailing_clean_run(self):
        word = b"12" * 8 + b"2"
        self.assertEqual(self.encode(word, 2)[-1], 0xF1)
        self.assertEqual(self.decode(self.encode(word, 2)), word)
        clean = b"12" * 40000
        binary = self.encode(clean, 2)
        self.assertEqual(binary, packet(2, len(clean), [0], []))
        self.assertEqual(self.decode(binary), clean)

    def test_stream_buffer_boundaries_and_outer_xz(self):
        word = b"123" + b"1" * 3000000
        for bits in [4, 6]:
            binary = self.encode(word, 3, "--bits", bits)
            self.assertGreater(len(binary), 65536)
            self.assertEqual(self.decode(self.xz_round_trip(binary)), word)
        # Decoded length 26 puts a literal Ctrl-Z in the binary header on Windows.
        control_z = self.encode(b"12" * 13, 2)
        self.assertEqual(control_z[8], 0x1A)
        self.assertEqual(self.decode(control_z), b"12" * 13)

    def test_generated_encodable_words(self):
        rng = random.Random(20261017)
        for n in range(2, 9):
            for _ in range(5):
                word = bytearray(ONE_BASED[:n])
                for _ in range(12):
                    word.append(rng.choice(word[-(n+8):]))
                with self.subTest(n=n, word=word):
                    self.assertEqual(self.decode(self.encode(bytes(word), n)), bytes(word))
        for n in [3, 13, 17, 31, 32]:
            word = bytearray(ONE_BASED[:n])
            for _ in range(200):
                word.append(rng.choice(word[-(n+32):]))
            self.assertEqual(self.decode(self.encode(word, n, "--bits=6")), word)

    def test_corpus_round_trips(self):
        paths = [
            (5, "words/5/153/153-recursive.txt", None),
            (6, "words/6/872/872-treelike.tar.xz", "872-treelike/872-00001.txt"),
            (7, "words/7/5913/5913-palindromic.txt", None),
            (8, "words/8/46181/superperm8_46181_1.txt", None),
            (9, "words/9/408731/superperm9_408731_19_canonical.spra.xz", None),
        ]
        for n, relative, member in paths:
            with self.subTest(path=relative):
                if member:
                    with tarfile.open(ROOT / relative, "r:xz") as archive:
                        source = archive.extractfile(member).read()
                else:
                    source = (ROOT / relative).read_bytes()
                if relative.endswith(".spra.xz") and not member:
                    source = self.decode(lzma.decompress(source))
                for bits in [4, 6]:
                    binary = self.encode(source, n, "--bits", bits)
                    self.assertEqual(self.decode(self.xz_round_trip(binary)), b"".join(source.split()))

    def test_named_inputs_and_explicit_stdin(self):
        word = b"123121321"
        source = Path(self.directory.name) / "input word.txt"
        archive = Path(self.directory.name) / "input word.spra"
        source.write_bytes(word)
        encoded = self.run_codec(b"ignored stdin", "encode", 3, source)
        self.assertEqual(encoded.returncode, 0, encoded.stderr)
        self.assertEqual(encoded.stdout, self.encode(word, 3))
        archive.write_bytes(encoded.stdout)
        decoded = self.run_codec(b"ignored stdin", "decode", archive)
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(decoded.stdout, word)
        self.assertEqual(self.run_codec(word, "encode", 3, "-").stdout, encoded.stdout)
        self.assertEqual(self.run_codec(encoded.stdout, "decode", "-").stdout, word)

    def run_wrapper(self, data, *args):
        if not self.bash:
            self.skipTest("Bash is required to test spra-xz.sh")
        environment = self.xz_environment.copy()
        environment["SPRA_CODEC"] = self.binary.as_posix()
        return subprocess.run([self.bash, (ROOT / "tools/spra-xz.sh").as_posix(),
                               *map(str, args)], input=data, capture_output=True,
                              env=environment, timeout=30)

    def test_xz_wrapper_streams_options_and_failures(self):
        for bits in [4, 6]:
            encoded = self.run_wrapper(b"11111", "encode", "--bits", bits, 3, "-", "--", "-6e")
            self.assertEqual(encoded.returncode, 0, encoded.stderr)
            self.assertEqual(lzma.decompress(encoded.stdout), self.encode(b"11111", 3, "--bits", bits))
            restored = self.run_wrapper(encoded.stdout, "decode", "--", "--memlimit-decompress=64MiB")
            self.assertEqual(restored.returncode, 0, restored.stderr)
            self.assertEqual(restored.stdout, b"11111")
            # xz can emit the whole SPRA payload before noticing a missing footer;
            # pipefail must preserve that upstream failure even if decode succeeds.
            self.assertNotEqual(self.run_wrapper(encoded.stdout[:-1], "decode").returncode, 0)
        self.assertNotEqual(self.run_wrapper(b"bad", "encode", 3, "--", "-6e").returncode, 0)
        self.assertNotEqual(self.run_wrapper(lzma.compress(b"bad"), "decode").returncode, 0)
        self.assertNotEqual(self.run_wrapper(b"", "encode", 3, "--", "--not-an-xz-option").returncode, 0)
        self.assertNotEqual(self.run_wrapper(b"", "decode", "one", "two").returncode, 0)
        self.assertNotEqual(self.run_wrapper(b"", "unknown").returncode, 0)
        help_result = self.run_wrapper(b"", "--help")
        self.assertEqual(help_result.returncode, 0)
        self.assertIn(b"-6e", help_result.stdout)

    def test_xz_wrapper_named_paths_with_spaces(self):
        source = Path(self.directory.name) / "input word.txt"
        archive = Path(self.directory.name) / "packed word.spra.xz"
        source.write_bytes(b"1 1111\n")
        encoded = self.run_wrapper(b"", "encode", 3, source.as_posix(), "--", "-6e", "--check=crc32")
        self.assertEqual(encoded.returncode, 0, encoded.stderr)
        self.assertEqual(source.read_bytes(), b"1 1111\n")
        archive.write_bytes(encoded.stdout)
        decoded = self.run_wrapper(b"", "decode", archive.as_posix())
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(decoded.stdout, b"11111")
        self.assertEqual(archive.read_bytes(), encoded.stdout)

    def test_benchmark_xz_arguments_and_baseline(self):
        source = Path(self.directory.name) / "benchmark word.txt"
        source.write_bytes(b"11111\n")
        result = subprocess.run([os.sys.executable, str(ROOT / "tools/benchmark_reduced_codec.py"),
                                 "3", str(source), "--codec", str(self.binary),
                                 "--baseline-codec", str(self.binary), "--xz-arg=-6e"],
                                capture_output=True, env=self.xz_environment, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["compression_arguments"], ["-6e"])
        self.assertEqual(report["layouts"], report["baseline_layouts"])
        for layout in report["layouts"]:
            self.assertEqual(layout["spra_version_byte"], 2)
            self.assertEqual(layout["logical_tuples"], 3)
            self.assertEqual(layout["stored_tokens"], 2)
            self.assertTrue(layout["round_trip_sha256_matches"])

    def test_unsupported_inputs_leave_encoder_stdout_empty(self):
        cases = [
            (b"12" * 8 + b"11", 2, b"depth 16"),
            (b"1234567899", 9, b"copy distance 1"),
            (b"12" + b"1" * 11 + b"2", 3, b"copy distance 12"),
            (b"112", 3, b"no earlier occurrence"),
            (b"123x", 3, b"invalid alphabet"),
            (b"124", 3, b"invalid alphabet"),
        ]
        for word, n, message in cases:
            with self.subTest(word=word, n=n):
                result = self.run_codec(word, "encode", n)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                self.assertIn(message, result.stderr)
        # Failure after enough tuples to flush the temporary file still publishes nothing.
        result = self.run_codec(b"123" + b"1" * 1000000 + b"x", "encode", 3)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"")

    def test_truncated_files_and_extra_bytes(self):
        for valid in [packet(3, 9, [0, 1], [0x31]),
                      packet(3, 9, [0, 1], [0x0C1], bits=6),
                      packet(3, 10, [0, 1], [0x0C1, 0x0C2], bits=6)]:
            for size in range(len(valid)):
                with self.subTest(size=size, layout=valid[5]):
                    result = self.run_codec(valid[:size], "decode")
                    self.assertEqual(result.returncode, 1)
                    self.assertNotEqual(result.stderr, b"")
            result = self.run_codec(valid + b"\x00", "decode")
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"extra bytes", result.stderr)

    def test_six_bit_padding_and_tuple_semantics(self):
        valid = packet(3, 9, [0, 1], [0x0C1], bits=6)
        for padding in [1, 8, 15]:
            malformed = valid[:-1] + bytes([valid[-1] | padding])
            result = self.run_codec(malformed, "decode")
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"padding", result.stderr)
        for raw, message in [(3, b"not backward"), (32, b"available history"),
                             (64, b"no preceding tuple")]:
            result = self.run_codec(packet(3, 4, [0, 1], [raw], bits=6), "decode")
            self.assertEqual(result.returncode, 1)
            self.assertIn(message, result.stderr)

    def test_invalid_headers_and_tuple_semantics(self):
        valid = packet(3, 9, [0, 1], [0x31])
        for offset, value in [(0, ord("X")), (4, 3), (5, 0x35),
                              (6, 2), (7, 0), (7, 33), (24, 3)]:
            malformed = bytearray(valid)
            malformed[offset] = value
            with self.subTest(offset=offset, value=value):
                result = self.run_codec(malformed, "decode")
                self.assertEqual(result.returncode, 1)
                self.assertNotEqual(result.stderr, b"")
        cases = [
            (packet(3, 2, [0, 1], [0x01]), b"tuple count"),
            (packet(3, 3, [0, 1], [0x11]), b"exceeds decoded length"),
            (packet(3, 3, [0, 1], [0x03]), b"not backward"),
            (packet(3, 3, [0, 1], [0x08]), b"available history"),
            (packet(3, 4, [0, 1], [0x10]), b"no preceding tuple"),
            (packet(3, 4, [0, 0], [0x02]), b"repeated suffix"),
        ]
        for data, message in cases:
            with self.subTest(message=message):
                result = self.run_codec(data, "decode")
                self.assertEqual(result.returncode, 1)
                self.assertIn(message, result.stderr)

    def test_arguments_and_help(self):
        for args in [[], ["unknown"], ["encode"], ["encode", 0],
                     ["encode", 33], ["encode", "3foo"],
                     ["encode", "--bits"], ["encode", "--bits", 5, 3],
                     ["encode", "--bits="], ["encode", "--bits=6foo", 3],
                     ["encode", "--bits", 999999999999999999, 3],
                     ["encode", "--bits", 4, "--bits=6", 3],
                     ["decode", "--bits", 6],
                     ["encode", "-z", "--zero-based", 3],
                     ["decode", "-z"], ["decode", "a", "b"],
                     ["encode", 3, "--unknown"]]:
            with self.subTest(args=args):
                result = self.run_codec(b"", *args)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                self.assertNotEqual(result.stderr, b"")
        for args in [("--help",), ("encode", "--help"), ("decode", "-h")]:
            result = self.run_codec(b"", *args)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b"stdout", result.stdout)
        result = self.run_codec(b"", "decode", Path(self.directory.name) / "missing.spra")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"cannot open input", result.stderr)


if __name__ == "__main__":
    unittest.main()
