# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.

"""Compile and check paint-waste against an independent window-set oracle."""
import itertools
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PaintWasteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.directory.name) / "paint-waste.exe"
        subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra",
                        "-pedantic", str(ROOT / "tools/reduced_alphabet.cpp"),
                        "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_word(self, word, *args):
        # Disconnect the controlling terminal so prompt tests never read from
        # the person running this suite. Word input still uses its own pipe.
        terminal_options = ({"creationflags": subprocess.DETACHED_PROCESS}
                            if os.name == "nt" else {"start_new_session": True})
        return subprocess.run([str(self.binary), *map(str, args)], input=word,
                              text=True, capture_output=True, timeout=15,
                              **terminal_options)

    def stats(self, word, n, *flags):
        result = self.run_word(word, "-s", n, *flags)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertNotIn("\x1b", result.stdout)
        return dict((key, int(value)) for key, value in
                    (line.split(": ") for line in result.stdout.splitlines()))

    def test_concrete_valid_word(self):
        self.assertEqual(self.stats("123121321", 3), {
            "length": 9, "symbols": 9, "n": 3, "windows": 7,
            "dirty": 1, "clean": 6, "max_dirty_run": 1,
            "max_clean_run": 3, "dirty_runs": 1, "clean_runs": 2,
            "required_permutations": 6, "distinct_permutations": 6,
            "missing_permutations": 0, "repeated_permutation_windows": 0,
            "valid_superpermutation": 1})

    def test_independent_oracle(self):
        rng = random.Random(42)
        for n in range(1, 6):
            alphabet = "12345"[:n]
            permutations = {"".join(p) for p in itertools.permutations(alphabet)}
            words = ["", alphabet[:-1], alphabet * 3]
            words += ["".join(rng.choice(alphabet) for _ in range(80)) for _ in range(5)]
            for word in words:
                with self.subTest(n=n, word=word):
                    windows = [word[i:i+n] for i in range(max(0, len(word)-n+1))]
                    clean = [w for w in windows if w in permutations]
                    stats = self.stats(word, n)
                    self.assertEqual(stats["length"], len(word))
                    self.assertEqual(stats["windows"], len(windows))
                    self.assertEqual(stats["clean"], len(clean))
                    self.assertEqual(stats["dirty"], len(windows)-len(clean))
                    self.assertEqual(stats["distinct_permutations"], len(set(clean)))
                    self.assertEqual(stats["missing_permutations"], math.factorial(n)-len(set(clean)))
                    self.assertEqual(stats["repeated_permutation_windows"], len(clean)-len(set(clean)))
                    self.assertEqual(stats["valid_superpermutation"], int(set(clean) == permutations))

    def test_default_order_decimal_statistics_and_long_depth(self):
        word = "12" * 250 + "11"
        result = self.run_word(word, 2)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        before, statistics = result.stdout.split("statistics:\n")
        self.assertIn("reduced_alphabet_words:\n[1f4:2]", before)
        short = self.run_word(word, 2, "-s")
        self.assertEqual(statistics, short.stdout)
        self.assertEqual(self.stats(word, 2)["max_clean_run"], 500)

    def test_whitespace_and_corpus(self):
        self.assertEqual(self.stats("1 23\r\n121\t321", 3), self.stats("123121321", 3))
        stats = self.stats((ROOT / "words/5/153/153-recursive.txt").read_text(), 5)
        self.assertEqual(stats["length"], 153)
        self.assertEqual(stats["distinct_permutations"], 120)
        self.assertEqual(stats["valid_superpermutation"], 1)

    def test_zero_based_labels_and_paint(self):
        self.assertEqual(self.stats("012010210", 3, "-z"), self.stats("123121321", 3))
        self.assertEqual(self.stats("012010210", 3, "--zero-based"), self.stats("123121321", 3))
        result = self.run_word("012010210", "-z", 3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith("01201\x1b[31m0\x1b[0m210\n"))
        self.assertIn("[3:3]", result.stdout)
        self.assertEqual(self.stats("000", 1, "-z")["valid_superpermutation"], 1)
        for word in ["123", "01a", "012A"]:
            self.assertNotEqual(self.run_word(word, "-z", "-s", 3).returncode, 0)

    def test_zero_based_oracle(self):
        rng = random.Random(123)
        for n in range(1, 7):
            alphabet = "012345"[:n]
            word = "".join(rng.choice(alphabet) for _ in range(150))
            windows = [word[i:i+n] for i in range(len(word)-n+1)]
            clean = [w for w in windows if len(set(w)) == n]
            stats = self.stats(word, n, "-z")
            self.assertEqual(stats["distinct_permutations"], len(set(clean)))
            self.assertEqual(stats["clean"], len(clean))
            self.assertEqual(stats["missing_permutations"], math.factorial(n)-len(set(clean)))

    def test_large_alphabets_statistics_only(self):
        for n in [10, 11, 12, 13]:
            for alphabet, flags in [("0123456789ABC", ["-z"]), ("123456789ABCD", [])]:
                stats = self.stats(alphabet[:n], n, *flags)
                self.assertEqual(stats["length"], n)
                self.assertEqual(stats["clean"], 1)
                self.assertEqual(stats["required_permutations"], math.factorial(n))
                self.assertEqual(stats["missing_permutations"], math.factorial(n)-1)
                self.assertEqual(stats["valid_superpermutation"], 0)

    def test_large_alphabet_requires_consent_without_terminal(self):
        result = self.run_word("0123456789", "-z", 10)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("rerun with -s", result.stderr)

    def test_stream_buffer_boundary(self):
        stats = self.stats("12" * 40000, 2)
        self.assertEqual(stats["length"], 80000)
        self.assertEqual(stats["clean"], 79999)
        self.assertEqual(stats["distinct_permutations"], 2)
        self.assertEqual(stats["repeated_permutation_windows"], 79997)
        self.assertEqual(stats["valid_superpermutation"], 1)

    def test_bracket_output_preserves_statistics_and_reduced_pairs(self):
        for word, n, flags, painted in [
                ("123121321", 3, [], "12312[1]321"),
                ("012010210", 3, ["-z"], "01201[0]210"),
                ("1111", 3, [], "11[1][1]"),
                ("123", 3, [], "123"), ("", 3, [], "")]:
            with self.subTest(word=word):
                normal = self.run_word(word, n, *flags)
                result = self.run_word(word, "-m", n, *flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertNotIn("\x1b", result.stdout)
                first_line, remainder = result.stdout.split("\n", 1)
                self.assertEqual(first_line, painted)
                self.assertEqual(remainder, normal.stdout.split("\n", 1)[1])
                self.assertEqual(result.stdout.split("statistics:\n")[1],
                                 self.run_word(word, "-s", n, *flags).stdout)
                after_n = self.run_word(word, n, *flags, "-m")
                self.assertEqual(after_n.stdout, result.stdout)

    def test_bracket_flag_with_short_mode(self):
        for word, n, flags in [("123121321", 3, []),
                               ("0123456789A", 11, ["-z"])]:
            expected = self.run_word(word, "-s", n, *flags).stdout
            for args in [("-m", "-s", n, *flags), (n, *flags, "-s", "-m")]:
                result = self.run_word(word, *args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertEqual(result.stdout, expected)
        # Bracket painting does not authorize a statistics-only downgrade.
        result = self.run_word("0123456789A", "-z", "-m", 11)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("rerun with -s", result.stderr)

    def test_bracket_output_errors_leave_stdout_empty(self):
        for word, args in [("12x", ["-m", 3]), ("124", ["-m", 3]),
                           ("123", ["-m", "-m", 3]), ("123", ["-m", 14])]:
            result = self.run_word(word, *args)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertNotEqual(result.stderr, "")

    def test_rejected_arguments_and_input(self):
        for args in [[], [0], [14], ["3foo"], [3, "--unknown"], ["-s", "-s", 3],
                     ["-z", "--zero-based", 3]]:
            self.assertNotEqual(self.run_word("123", *args).returncode, 0)
        for word in ["124", "120", "12x"]:
            self.assertNotEqual(self.run_word(word, "-s", 3).returncode, 0)
        self.assertEqual(self.run_word("", "--help").returncode, 0)


if __name__ == "__main__":
    unittest.main()
