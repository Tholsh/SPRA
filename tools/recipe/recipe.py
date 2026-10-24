#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jakub Halfar
"""recipe.py - SPRA recipe containers (.sprc): a constructed superpermutation stored as the choices of its
construction, decoded by running the construction.  The format is in docs/recipe_container.md.

usage:
  recipe.py info FILE                       header, generator check, parts and steps
  recipe.py decode FILE [-o OUT|-] [--work DIR] [--keep] [--fallback] [--quiet]
                                            run the steps, check length and SHA-256 of the letters; with -o write
                                            the word (letters only, no final newline); without -o only check.
                                            --fallback decodes the SPRA fallback instead of running generators.
  recipe.py pack OUT --n N --alphabet A --length L --sha256 H --steps FILE [--part NAME=FILE ...]
                 [--fallback FILE.spra.xz]
  recipe.py add-fallback IN FILE.spra.xz OUT    copy of IN with an SPRA + xz fallback
  recipe.py extract-fallback IN OUT.spra.xz
  recipe.py hashes                          SHA-256 of the source manifest of every known generator

  pack helpers (all outputs are parts):
  recipe.py selpack SEL OUT [m r]           a whole selection, or the (m, r) block solution, as 2 bytes per loop
  recipe.py seldiff A B OUT [--rows-only]   the loops where selection B differs from A
  recipe.py transport SEL k OUT             k transports of a selection (to build the A of a diff)
  recipe.py planenc PLAN OUT                a plan text as PLC2 columns (byte-exact round trip)
  recipe.py plandec IN OUT

Exit status: 0 ok, 1 usage or I/O error, 2 container refused (format, limits, generator source hash), 3 the
decoded word does not have the expected length or SHA-256, 4 a generator step failed.
"""
import hashlib
import importlib.util
import itertools
import lzma
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GEN = os.path.join(HERE, "gen")
MAGIC = b"SPRC"
VERSION = 1
PMAGIC = b"RCP1"
PLC_MAGIC = b"PLC2"
XZ = dict(format=lzma.FORMAT_XZ, preset=9 | lzma.PRESET_EXTREME)
MAX_PAYLOAD = 64 << 20          # decompressed payload
MAX_FALLBACK = 1 << 32
ALPH = "0123456789ABCDEFGHIJKLMNOPQRSTUV"
SPRA_ONE = "123456789ABCDEFGHIJKLMNOPQRSTUVW"
SPRA_ZERO = "0123456789ABCDEFGHIJKLMNOPQRSTUV"
CHUNK = 1 << 22
CREATED = set()                 # output files this run opened (removed again on failure)

# Generators: id -> family, folder under gen/, the files of the source manifest (decode needs exactly these).
GENERATORS = {
    "pantone-py/1": dict(family="pantone", dir="pantone_py",
                         files=["applyplan.py", "build12.py", "gcore.py", "gen12.py", "geng.py", "gregion.py",
                                "hybrid3.py"]),
    "gen14/1": dict(family="pantone", dir="gen14", files=["gen14.c"]),
}

# Steps: op -> (generator id or None for the container layer, argument kinds, allowed flags).
#   i = input name, o = output name, k = small integer, i+ = one or more input names
OPS = {
    "unpack": (None, ["i", "o"], ()),
    "patch": (None, ["i", "i", "o"], ()),
    "plan": (None, ["i", "o"], ()),
    "transport": ("pantone-py/1", ["i", "k", "o"], ()),
    "hybrid3": ("pantone-py/1", ["i", "i", "k", "i", "o"], ("--fsd",)),
    "build12": ("pantone-py/1", ["i", "i", "i", "o"], ()),
    "gregion": ("pantone-py/1", ["i", "i", "o", "i+"], ()),
    "geng": ("pantone-py/1", ["i", "o", "o"], ("--noliteral",)),
    "applyplan": ("pantone-py/1", ["i", "i", "i", "o"], ()),
    "gen14": ("gen14/1", ["i", "i", "o"], ()),
}
K_RANGE = {"transport": (1, 4), "hybrid3": (1, 8)}
NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
RESERVED = {"CON", "PRN", "AUX", "NUL"} | {f"{p}{i}" for p in ("COM", "LPT") for i in range(1, 10)}


class Refused(Exception):
    """The container is refused (exit 2)."""


class Mismatch(Exception):
    """The word does not have the expected length or hash (exit 3)."""


class StepFailed(Exception):
    """A generator step failed (exit 4)."""


def say(*a):
    print(*a, file=sys.stderr, flush=True)


# ---------------------------------------------------------------- generator source hashes
def source_sha(gid):
    """SHA-256 of the manifest text: one line "<sha256 of the file, CR LF read as LF>  <file name>\\n" per file of
    the generator, in the listed (sorted) order."""
    g = GENERATORS[gid]
    lines = []
    for f in g["files"]:
        with open(os.path.join(GEN, g["dir"], f), "rb") as fh:
            b = fh.read().replace(b"\r\n", b"\n")
        lines.append("%s  %s\n" % (hashlib.sha256(b).hexdigest(), f))
    return hashlib.sha256("".join(lines).encode()).digest()


# ---------------------------------------------------------------- container read / write
def u8(b, p):
    if p >= len(b):
        raise Refused("container truncated")
    return b[p], p + 1


def take(b, p, n):
    if p + n > len(b):
        raise Refused("container truncated")
    return b[p:p + n], p + n


def parse_container(b):
    c = {}
    m, p = take(b, 0, 4)
    if m != MAGIC:
        raise Refused("not a recipe container (magic %r)" % m)
    ver, p = u8(b, p)
    if ver != VERSION:
        raise Refused("container version %d is not supported (this reader: %d)" % (ver, VERSION))
    n, p = u8(b, p)
    flags, p = u8(b, p)
    if not 1 <= n <= 32:
        raise Refused("n = %d outside 1..32" % n)
    if flags:
        raise Refused("reserved flags byte is %d, must be 0" % flags)
    a, p = take(b, p, n)
    if len(set(a)) != n or any(not 0x21 <= x <= 0x7E for x in a):
        raise Refused("alphabet must be n distinct printable ASCII characters")
    c["n"], c["alphabet"] = n, a.decode()
    raw, p = take(b, p, 8)
    (c["length"],) = struct.unpack("<Q", raw)
    c["sha256"], p = take(b, p, 32)
    fl, p = u8(b, p)
    if not 1 <= fl <= 64:
        raise Refused("family tag length %d outside 1..64" % fl)
    fam, p = take(b, p, fl)
    c["family"] = fam.decode("ascii", "replace")
    g, p = u8(b, p)
    if not 1 <= g <= 16:
        raise Refused("generator count %d outside 1..16" % g)
    c["generators"] = []
    for _ in range(g):
        il, p = u8(b, p)
        if not 1 <= il <= 64:
            raise Refused("generator id length %d outside 1..64" % il)
        gid, p = take(b, p, il)
        h, p = take(b, p, 32)
        c["generators"].append((gid.decode("ascii", "replace"), h))
    meth, p = u8(b, p)
    if meth != 1:
        raise Refused("payload method %d unknown (1 = xz)" % meth)
    raw, p = take(b, p, 8)
    (pl,) = struct.unpack("<Q", raw)
    if pl > MAX_PAYLOAD:
        raise Refused("payload larger than 64 MiB")
    c["payload"], p = take(b, p, pl)
    fk, p = u8(b, p)
    c["fallback_kind"], c["fallback"] = fk, b""
    if fk not in (0, 1):
        raise Refused("fallback kind %d unknown (0 none, 1 SPRA + xz)" % fk)
    if fk:
        raw, p = take(b, p, 8)
        (fl,) = struct.unpack("<Q", raw)
        if fl > MAX_FALLBACK:
            raise Refused("fallback larger than 4 GiB")
        c["fallback"], p = take(b, p, fl)
    if p != len(b):
        raise Refused("%d bytes after the end of the container" % (len(b) - p))
    return c


def build_container(c):
    out = bytearray(MAGIC)
    out += bytes([VERSION, c["n"], 0]) + c["alphabet"].encode()
    out += struct.pack("<Q", c["length"]) + c["sha256"]
    fam = c["family"].encode()
    out += bytes([len(fam)]) + fam + bytes([len(c["generators"])])
    for gid, h in c["generators"]:
        out += bytes([len(gid)]) + gid.encode() + h
    out += bytes([1]) + struct.pack("<Q", len(c["payload"])) + c["payload"]
    if c.get("fallback"):
        out += bytes([1]) + struct.pack("<Q", len(c["fallback"])) + c["fallback"]
    else:
        out += bytes([0])
    parse_container(bytes(out))        # the writer never writes what the reader refuses
    return bytes(out)


def read_container(path):
    with open(path, "rb") as f:
        return parse_container(f.read())


def parse_payload(data):
    d = lzma.LZMADecompressor(format=lzma.FORMAT_XZ)
    try:
        raw = d.decompress(data, max_length=MAX_PAYLOAD + 1)
    except lzma.LZMAError as e:
        raise Refused("payload is not a valid xz stream: %s" % e)
    if len(raw) > MAX_PAYLOAD:
        raise Refused("decompressed payload larger than 64 MiB")
    if not d.eof or d.unused_data:
        raise Refused("payload must be exactly one complete xz stream")
    if raw[:4] != PMAGIC:
        raise Refused("payload magic %r, expected RCP1" % raw[:4])
    if len(raw) < 6:
        raise Refused("payload truncated")
    (cnt,) = struct.unpack_from("<H", raw, 4)
    p, parts = 6, {}
    for _ in range(cnt):
        if p >= len(raw):
            raise Refused("payload truncated")
        nl = raw[p]
        name = raw[p + 1:p + 1 + nl].decode("ascii", "replace")
        p += 1 + nl
        if p + 4 > len(raw):
            raise Refused("payload truncated")
        (ln,) = struct.unpack_from("<I", raw, p)
        p += 4
        if p + ln > len(raw):
            raise Refused("payload truncated")
        check_name(name)
        if name in parts:
            raise Refused("part %s appears twice" % name)
        parts[name] = raw[p:p + ln]
        p += ln
    if p != len(raw):
        raise Refused("bytes after the last part")
    if "steps" not in parts:
        raise Refused("no part named steps")
    return parts


def build_payload(parts):
    out = bytearray(PMAGIC) + struct.pack("<H", len(parts))
    for name, data in parts.items():
        check_name(name)
        out += bytes([len(name)]) + name.encode() + struct.pack("<I", len(data)) + data
    return lzma.compress(bytes(out), **XZ)


def check_name(name):
    if not NAME_RE.match(name) or name.split(".")[0].upper() in RESERVED:
        raise Refused("bad file name %r" % name)


# ---------------------------------------------------------------- steps
def parse_steps(text, parts, generators):
    """Validate the steps against the parts and the generators of the container.  Returns a list of
    (op, args, flags)."""
    try:
        text = text.decode("ascii")
    except UnicodeDecodeError:
        raise Refused("steps must be ASCII")
    if not text.endswith("\n"):
        raise Refused("steps must end with a line feed")
    have = set(parts) - {"steps"}
    gids = {g for g, _ in generators}
    steps = []
    lines = text[:-1].split("\n")
    for ln, line in enumerate(lines, 1):
        tok = line.split(" ")
        op = tok[0]
        if op not in OPS:
            raise Refused("steps line %d: unknown operation %r" % (ln, op))
        gid, kinds, allowed = OPS[op]
        if gid and gid not in gids:
            raise Refused("steps line %d: %s needs generator %s, which the container does not list" % (ln, op, gid))
        flags = [t for t in tok[1:] if t.startswith("--")]
        args = [t for t in tok[1:] if not t.startswith("--")]
        if any(f not in allowed for f in flags) or len(set(flags)) != len(flags):
            raise Refused("steps line %d: flags %s not allowed for %s" % (ln, flags, op))
        plus = kinds[-1] == "i+"
        if (len(args) < len(kinds)) if plus else (len(args) != len(kinds)):
            raise Refused("steps line %d: %s takes %d arguments" % (ln, op, len(kinds)))
        outs = []
        for i, a in enumerate(args):
            kd = kinds[min(i, len(kinds) - 1)]
            if kd == "k":
                lo, hi = K_RANGE[op]
                if not (a.isdigit() and lo <= int(a) <= hi):
                    raise Refused("steps line %d: %r is not an integer in %d..%d" % (ln, a, lo, hi))
                continue
            check_name(a)
            if a == "steps":
                raise Refused("steps line %d: the name steps is reserved" % ln)
            if kd.startswith("i"):
                if a not in have:
                    raise Refused("steps line %d: input %s is neither a part nor an earlier output" % (ln, a))
            else:
                if a in have or a in outs:
                    raise Refused("steps line %d: output %s already exists" % (ln, a))
                outs.append(a)
        if "word" in outs and ln != len(lines):
            raise Refused("steps line %d: only the last step may write word" % ln)
        have.update(outs)
        steps.append((op, args, flags))
    if not steps or "word" not in [a for a in steps[-1][1]]:
        raise Refused("the last step must write word")
    if steps[-1][0] not in ("applyplan", "gen14"):
        raise Refused("word must be written by applyplan or gen14")
    return steps


# ---------------------------------------------------------------- selections (container layer)
def canon(x):
    i = x.index(0)
    return x[i:] + x[:i]


def all_loops(K):
    """All cyclic orders of 0..K-1, as tuples starting with 0, in increasing (lexicographic) order."""
    return [(0,) + p for p in itertools.permutations(range(1, K))]


def block_loops(m, r):
    """The loops of the (m, r) block: cyclic orders of K = m + r letters in which 0..m-1 stand in the cyclic order
    (0 1 .. m-1), in increasing order."""
    base = [tuple(range(m))]
    for t in range(m, m + r):
        base = [c[:j] + (t,) + c[j:] for c in base for j in range(1, len(c) + 1)]
    return sorted(set(canon(c) for c in base))


def read_sel(path):
    """A selection file: "x v" lines (v a number or F/S/D), # comments; or Pantone's construction-input.txt
    (CIRCLE4380V1, row type 0 = full, 2 = short).  Returns dict loop -> (x, v), loops without a row as (loop, 0)."""
    with open(path) as f:
        text = f.read()
    sel = {}
    if text.startswith("CIRCLE4380V1"):
        tok = text.split()
        for i in range(int(tok[1])):
            x = tuple(ALPH.index(ch) for ch in tok[3 + 2 * i])
            sel[canon(x)] = (x, len(x) if tok[4 + 2 * i] == "0" else len(x) - 2)
        return sel
    for line in text.split("\n"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        a, ty = line.split()[:2]
        x = tuple(ALPH.index(ch) for ch in a)
        K = len(x)
        v = {"F": K, "S": K - 2, "D": 0}[ty] if ty in ("F", "S", "D") else int(ty)
        if canon(x) in sel:
            raise ValueError("loop of %s appears twice in %s" % (a, path))
        sel[canon(x)] = (x if v else canon(x), v)
    return sel


def write_sel(path, sel, fsd=True):
    """One line per loop in increasing order: "x F|S|D" when fsd and every row is full or short, else "x v"."""
    K = len(next(iter(sel)))
    use = fsd and all(v in (0, K, K - 2) for (_, v) in sel.values())
    with open(path, "w", newline="\n") as f:
        for L in sorted(sel):
            x, v = sel[L]
            t = {0: "D", K: "F", K - 2: "S"}[v] if use else str(v)
            f.write("".join(ALPH[c] for c in x) + " " + t + "\n")


def row_of(L, v, rho):
    K = len(L)
    if not 0 <= v <= K or not 0 <= rho < K or (v == 0 and rho):
        raise Refused("bad row (v %d, rotation %d) for K = %d" % (v, rho, K))
    return ((L[rho:] + L[:rho]) if v else L, v)


def rot_of(L, x):
    for r in range(len(L)):
        if L[r:] + L[:r] == tuple(x):
            return r
    raise ValueError("x is not a rotation of its loop")


def op_unpack(data, out):
    if len(data) < 4:
        raise Refused("selection pack truncated")
    K, kind, m, r = data[:4]
    if not 3 <= K <= 20 or kind not in (0, 1):
        raise Refused("selection pack header (K %d, kind %d) invalid" % (K, kind))
    if kind == 0:
        if m or r:
            raise Refused("whole-selection pack with m, r set")
        if math.factorial(K - 1) > 1 << 24:
            raise Refused("selection too large")
        loops = all_loops(K)
    else:
        if m < 2 or r < 1 or m + r != K:
            raise Refused("block pack with m + r != K")
        loops = block_loops(m, r)
    if len(data) != 4 + 2 * len(loops):
        raise Refused("selection pack has %d bytes, expected %d" % (len(data), 4 + 2 * len(loops)))
    sel = {L: row_of(L, data[4 + 2 * i], data[5 + 2 * i]) for i, L in enumerate(loops)}
    write_sel(out, sel, fsd=(kind == 0))


def op_patch(sel_path, data, out):
    sel = read_sel(sel_path)
    if len(data) < 5:
        raise Refused("diff truncated")
    K, cnt = struct.unpack_from("<BI", data, 0)
    if K != len(next(iter(sel))):
        raise Refused("diff for K = %d applied to a selection with K = %d" % (K, len(next(iter(sel)))))
    if len(data) != 5 + 6 * cnt:
        raise Refused("diff has %d bytes, expected %d" % (len(data), 5 + 6 * cnt))
    loops = all_loops(K)
    last = -1
    for j in range(cnt):
        i, v, rho = struct.unpack_from("<IBB", data, 5 + 6 * j)
        if not last < i < len(loops):
            raise Refused("diff indices must increase and stay below (K-1)!")
        last = i
        sel[loops[i]] = row_of(loops[i], v, rho)
    write_sel(out, sel)


def selpack(sel_path, out, m=None, r=None):
    sel = read_sel(sel_path)
    K = len(next(iter(sel)))
    loops = all_loops(K) if m is None else block_loops(m, r)
    if len(loops) != len(sel) or set(loops) != set(sel):
        raise ValueError("the loops of %s are not the expected list" % sel_path)
    body = bytearray([K, 0 if m is None else 1, m or 0, r or 0])
    for L in loops:
        x, v = sel[L]
        body += bytes([v, rot_of(L, x) if v else 0])
    with open(out, "wb") as f:
        f.write(body)


def seldiff(a_path, b_path, out, rows_only=False):
    A, B = read_sel(a_path), read_sel(b_path)
    K = len(next(iter(B)))
    rows = []
    for i, L in enumerate(all_loops(K)):
        if A.get(L) != B[L] and (not rows_only or A[L][1]):
            rows.append((i, B[L][1], rot_of(L, B[L][0]) if B[L][1] else 0))
    with open(out, "wb") as f:
        f.write(struct.pack("<BI", K, len(rows)))
        for i, v, rho in rows:
            f.write(struct.pack("<IBB", i, v, rho))
    say("%d loops differ" % len(rows))


# ---------------------------------------------------------------- plans (PLC2)
def uv(v, out):
    while v >= 128:
        out.append((v & 127) | 128)
        v >>= 7
    out.append(v)


def rv(b, p):
    v = s = 0
    while True:
        c = b[p]
        p += 1
        v |= (c & 127) << s
        s += 7
        if c < 128:
            return v, p


def zz(d):
    return 2 * d if d >= 0 else -2 * d - 1


def unzz(z):
    return z // 2 if z % 2 == 0 else -(z + 1) // 2


def plan_line(line):
    t = line.split(" ")
    if len(t) == 1 and t[0].isdigit() and str(int(t[0])) == t[0]:
        return "#", [int(t[0])]
    if len(t) >= 2 and len(t[0]) == 1 and ("A" <= t[0] <= "Z" or "a" <= t[0] <= "z") and \
            all(x.isdigit() and str(int(x)) == x for x in t[1:]):
        return t[0], [int(x) for x in t[1:]]
    return None


def planenc(src, dst):
    with open(src, "rb") as f:
        text = f.read().decode("ascii")
    lines = text.split("\n")
    final_lf = lines[-1] == ""
    if final_lf:
        lines.pop()
    kinds, verb, cols, crs, ar = [], [], {}, bytearray(), {}
    for ln in lines:
        crs.append(1 if ln.endswith("\r") else 0)
        ln = ln[:-1] if crs[-1] else ln
        p = plan_line(ln)
        if p is not None and ar.get(p[0], len(p[1])) != len(p[1]):
            p = None                        # a line type keeps one arity; other lines stay verbatim
        if p is None:
            kinds.append("\0")
            verb.append(ln)
            continue
        ty, vals = p
        ar[ty] = len(vals)
        kinds.append(ty)
        for i, v in enumerate(vals):
            cols.setdefault((ty, i), []).append(v)
    out = bytearray(PLC_MAGIC)
    out.append(1 if final_lf else 0)
    vb = "\n".join(verb).encode()
    uv(len(vb), out)
    out += vb
    kb = "".join(kinds).encode()
    uv(len(kb), out)
    out += kb + crs
    uv(len(ar), out)
    for ty in sorted(ar):
        out += ty.encode()
        uv(ar[ty], out)
    for key in sorted(cols):
        vals = cols[key]
        raw, dl, prev = bytearray(), bytearray(), 0
        for v in vals:
            uv(v, raw)
            uv(zz(v - prev), dl)
            prev = v
        use_d = len(lzma.compress(bytes(dl), **XZ)) < len(lzma.compress(bytes(raw), **XZ))
        body = dl if use_d else raw
        out.append(1 if use_d else 0)
        uv(len(body), out)
        out += body
    with open(dst, "wb") as f:
        f.write(out)
    if plandec_bytes(bytes(out)) != text.encode():
        raise ValueError("PLC2 round trip failed")


def plandec_bytes(b):
    try:
        if b[:4] != PLC_MAGIC:
            raise Refused("plan part is not PLC2")
        final_lf = b[4] == 1
        p = 5
        n, p = rv(b, p)
        verb = b[p:p + n].decode("ascii").split("\n")
        p += n
        n, p = rv(b, p)
        kinds = b[p:p + n].decode("ascii")
        p += n
        crs = b[p:p + n]
        p += n
        nt, p = rv(b, p)
        ar = {}
        for _ in range(nt):
            ty = chr(b[p])
            p += 1
            ar[ty], p = rv(b, p)
        cols = {}
        for key in sorted((ty, i) for ty in ar for i in range(ar[ty])):
            use_d = b[p] == 1
            p += 1
            ln, p = rv(b, p)
            end = p + ln
            vals, prev = [], 0
            while p < end:
                v, p = rv(b, p)
                if use_d:
                    v = prev + unzz(v)
                    prev = v
                vals.append(v)
            cols[key] = vals
        if p != len(b):
            raise Refused("bytes after the last plan column")
        pos = dict.fromkeys(cols, 0)
        vi = 0
        lines = []
        for li, ty in enumerate(kinds):
            cr = "\r" if crs[li] else ""
            if ty == "\0":
                lines.append(verb[vi] + cr)
                vi += 1
                continue
            vals = []
            for i in range(ar[ty]):
                vals.append(cols[(ty, i)][pos[(ty, i)]])
                pos[(ty, i)] += 1
            lines.append((str(vals[0]) if ty == "#" else ty + " " + " ".join(map(str, vals))) + cr)
        return ("\n".join(lines) + ("\n" if final_lf else "")).encode()
    except (IndexError, KeyError, UnicodeDecodeError) as e:
        raise Refused("plan part damaged (%s)" % type(e).__name__)


# ---------------------------------------------------------------- the word: length and SHA-256 on the fly
class Sink:
    """Counts and hashes the letters, optionally writes them.  One final line feed of the generator output is not
    part of the word."""

    def __init__(self, out, translate=None):
        self.h = hashlib.sha256()
        self.n = 0
        self.pending = b""
        self.tr = translate
        self.f = None
        if out == "-":
            self.f = sys.stdout.buffer
        elif out:
            self.f = open(out, "wb")
            CREATED.add(out)

    def write(self, b):
        if not b:
            return
        if self.tr:
            b = b.translate(self.tr)
        b = self.pending + b
        self.pending = b[-1:]
        b = b[:-1]
        self.h.update(b)
        self.n += len(b)
        if self.f:
            self.f.write(b)

    def close(self):
        if self.pending and self.pending != b"\n":
            self.h.update(self.pending)
            self.n += 1
            if self.f:
                self.f.write(self.pending)
        self.pending = b""
        if self.f and self.f is not sys.stdout.buffer:
            self.f.close()
        elif self.f:
            self.f.flush()
        return self.n, self.h.digest()


# ---------------------------------------------------------------- decode
def load_gcore():
    spec = importlib.util.spec_from_file_location("gcore", os.path.join(GEN, "pantone_py", "gcore.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def transport_file(src, k, out):
    """k transports (gcore.transport) of the selection in src, written with gcore.write_gsel."""
    gc = load_gcore()
    sel = gc.read_any(src)
    for _ in range(k):
        sel = gc.transport(sel)
    gc.write_gsel(out, sel)


def run_logged(argv, cwd, log, stdout=None):
    with open(log, "ab") as lf:
        lf.write(("$ " + " ".join(argv) + "\n").encode())
        lf.flush()
        p = subprocess.run(argv, cwd=cwd, stdout=stdout or lf, stderr=lf, stdin=subprocess.DEVNULL)
    if p.returncode:
        with open(log, "rb") as lf:
            tail = lf.read()[-1500:].decode(errors="replace")
        raise StepFailed("%s exited with %d\n%s" % (os.path.basename(argv[1] if len(argv) > 1 else argv[0]),
                                                    p.returncode, tail))


def check_generators(c):
    bad = []
    for gid, h in c["generators"]:
        if gid not in GENERATORS:
            bad.append("generator %s is unknown to this reader" % gid)
        elif GENERATORS[gid]["family"] != c["family"]:
            bad.append("generator %s belongs to family %s, the container says %s" % (
                gid, GENERATORS[gid]["family"], c["family"]))
        elif source_sha(gid) != h:
            bad.append("generator %s: source hash %s, this reader has %s" % (gid, h.hex(), source_sha(gid).hex()))
    return bad


def decode(path, out=None, work=None, keep=False, use_fallback=False, quiet=False):
    c = read_container(path)
    log = (lambda *a: None) if quiet else say
    if use_fallback:
        return decode_fallback(c, out, work, keep, log)
    bad = check_generators(c)
    if bad:
        msg = "refused: " + "; ".join(bad)
        if c["fallback_kind"]:
            msg += " (the container has an SPRA fallback: decode with --fallback)"
        raise Refused(msg)
    parts = parse_payload(c["payload"])
    steps = parse_steps(parts["steps"], parts, c["generators"])
    wd = tempfile.mkdtemp(prefix="sprc-", dir=work)
    gp = os.path.join(GEN, "pantone_py")
    py = [sys.executable, "-E", "-s"]
    logf = os.path.join(wd, "steps.log")
    last_use = {}
    for si, (_, args, _) in enumerate(steps):
        for a in args:
            last_use[a] = si
    t0 = time.time()
    try:
        for name, data in parts.items():
            if name != "steps":
                with open(os.path.join(wd, name), "wb") as f:
                    f.write(data)
        j = lambda a: os.path.join(wd, a)  # noqa: E731
        result = None
        for si, (op, a, flags) in enumerate(steps):
            t = time.time()
            if op == "unpack":
                op_unpack(parts[a[0]] if a[0] in parts else open(j(a[0]), "rb").read(), j(a[1]))
            elif op == "patch":
                op_patch(j(a[0]), open(j(a[1]), "rb").read(), j(a[2]))
            elif op == "plan":
                with open(j(a[1]), "wb") as f:
                    f.write(plandec_bytes(open(j(a[0]), "rb").read()))
            elif op == "transport":
                transport_file(j(a[0]), int(a[1]), j(a[2]))
            elif op == "hybrid3":
                run_logged(py + [os.path.join(gp, "hybrid3.py")] + [j(a[0]), j(a[1]), a[2], j(a[3]), j(a[4])]
                           + flags, wd, logf)
            elif op == "build12":
                run_logged(py + [os.path.join(gp, "build12.py")] + [j(x) for x in a], wd, logf)
            elif op == "gregion":
                run_logged(py + [os.path.join(gp, "gregion.py"), "multi", j(a[0]), j(a[1]), j(a[2])]
                           + [j(x) for x in a[3:]] + ["--walks", "0:0"], wd, logf)
            elif op == "geng":
                run_logged(py + [os.path.join(gp, "geng.py"), j(a[0]), j(a[1]), "--table", j(a[2])] + flags,
                           wd, logf)
            elif op == "applyplan":
                run_logged(py + [os.path.join(gp, "applyplan.py")] + [j(x) for x in a], wd, logf)
                if a[3] == "word":
                    sink = Sink(out)
                    with open(j("word"), "rb") as f:
                        for b in iter(lambda: f.read(CHUNK), b""):
                            sink.write(b)
                    result = sink.close()
                    os.remove(j("word"))
            elif op == "gen14":
                exe = j("gen14.exe")
                if not os.path.exists(exe):
                    run_logged([os.environ.get("CC", "gcc"), "-O3", "-o", exe,
                                os.path.join(GEN, "gen14", "gen14.c")], wd, logf)
                with open(j(a[0])) as f, open(j(a[0]) + ".rows", "w", newline="\n") as g:
                    g.writelines(ln for ln in f if not ln.startswith("#"))   # gen14 reads lines of <= 255 bytes
                argv = [exe, "plan", j(a[0]) + ".rows", j(a[1]), "--nosha"]
                if a[2] == "word":
                    sink = Sink(out)
                    with open(logf, "ab") as lf:
                        lf.write(("$ " + " ".join(argv) + "\n").encode())
                        lf.flush()
                        p = subprocess.Popen(argv, cwd=wd, stdout=subprocess.PIPE, stderr=lf,
                                             stdin=subprocess.DEVNULL)
                        for b in iter(lambda: p.stdout.read(CHUNK), b""):
                            sink.write(b)
                        rc = p.wait()
                    if rc:
                        raise StepFailed("gen14 exited with %d (see %s)" % (rc, logf))
                    result = sink.close()
                else:
                    with open(j(a[2]), "wb") as fo:
                        run_logged(argv, wd, logf, stdout=fo)
            log("step %d/%d %s %s: %.1f s" % (si + 1, len(steps), op, " ".join(a + flags), time.time() - t))
            if not keep:
                for x in set(a):
                    if last_use.get(x) == si and x != "word" and os.path.exists(j(x)):
                        os.remove(j(x))
        n, h = result
        if n != c["length"] or h != c["sha256"]:
            raise Mismatch("decoded %d letters, SHA-256 %s; the container expects %d, %s" % (
                n, h.hex(), c["length"], c["sha256"].hex()))
        log("ok: %d letters, SHA-256 %s, %.0f s" % (n, h.hex(), time.time() - t0))
        return n, h
    except BaseException:
        if out in CREATED and os.path.exists(out):
            os.remove(out)
        if keep:
            say("work folder kept:", wd)
        raise
    finally:
        if not keep:
            shutil.rmtree(wd, ignore_errors=True)


def spra_codec(wd, log):
    env = os.environ.get("SPRA_CODEC")
    if env:
        return env
    for cand in ("build/reduced-codec", "build/reduced-codec.exe", "reduced-codec", "reduced-codec.exe"):
        p = os.path.join(ROOT, cand)
        if os.path.isfile(p):
            return p
    exe = os.path.join(wd, "reduced-codec.exe")
    log("building tools/reduced_codec.cpp")
    run_logged([os.environ.get("CXX", "g++"), "-std=c++20", "-O2", "-o", exe,
                os.path.join(ROOT, "tools", "reduced_codec.cpp")], wd, os.path.join(wd, "steps.log"))
    return exe


def decode_fallback(c, out, work, keep, log):
    if not c["fallback_kind"]:
        raise Refused("the container has no fallback")
    spra = lzma.decompress(c["fallback"], format=lzma.FORMAT_XZ)
    if spra[:4] != b"SPRA" or len(spra) < 24 or spra[7] != c["n"]:
        raise Refused("fallback is not an SPRA file for n = %d" % c["n"])
    labels = (SPRA_ZERO if spra[6] & 1 else SPRA_ONE)[:c["n"]]
    tr = bytes.maketrans(labels.encode(), c["alphabet"].encode())
    wd = tempfile.mkdtemp(prefix="sprc-", dir=work)
    t0 = time.time()
    try:
        with open(os.path.join(wd, "fallback.spra"), "wb") as f:
            f.write(spra)
        codec = spra_codec(wd, log)
        sink = Sink(out, tr)
        p = subprocess.Popen([codec, "decode", os.path.join(wd, "fallback.spra")], stdout=subprocess.PIPE,
                             stdin=subprocess.DEVNULL)
        for b in iter(lambda: p.stdout.read(CHUNK), b""):
            sink.write(b)
        if p.wait():
            raise StepFailed("SPRA decoder exited with %d" % p.returncode)
        n, h = sink.close()
        if n != c["length"] or h != c["sha256"]:
            raise Mismatch("fallback gives %d letters, SHA-256 %s; the container expects %d, %s" % (
                n, h.hex(), c["length"], c["sha256"].hex()))
        log("ok (fallback): %d letters, SHA-256 %s, %.0f s" % (n, h.hex(), time.time() - t0))
        return n, h
    except BaseException:
        if out in CREATED and os.path.exists(out):
            os.remove(out)
        raise
    finally:
        if not keep:
            shutil.rmtree(wd, ignore_errors=True)


# ---------------------------------------------------------------- pack and info
def pack(out, n, alphabet, length, sha, steps_path, part_specs, fallback=None):
    parts = {}
    with open(steps_path, "rb") as f:
        parts["steps"] = f.read().replace(b"\r\n", b"\n")
    for spec in part_specs:
        name, _, src = spec.partition("=")
        with open(src, "rb") as f:
            parts[name] = f.read()
    gids = sorted({OPS[line.split(" ")[0]][0] for line in parts["steps"].decode().split("\n") if line
                   and line.split(" ")[0] in OPS} - {None})
    gens = [(g, source_sha(g)) for g in gids]
    parse_steps(parts["steps"], parts, gens)
    fams = {GENERATORS[g]["family"] for g in gids}
    if len(fams) != 1:
        raise Refused("the steps use generators of %d families" % len(fams))
    c = dict(n=n, alphabet=alphabet, length=length, sha256=bytes.fromhex(sha), family=fams.pop(),
             generators=gens, payload=build_payload(parts), fallback=b"")
    if fallback:
        with open(fallback, "rb") as f:
            c["fallback"] = f.read()
    data = build_container(c)
    with open(out, "wb") as f:
        f.write(data)
    say("wrote %s: %d bytes (payload %d, fallback %d)" % (out, len(data), len(c["payload"]), len(c["fallback"])))


def info(path):
    c = read_container(path)
    size = os.path.getsize(path)
    print("file        %s (%d bytes)" % (path, size))
    print("format      SPRC version %d" % VERSION)
    print("word        n = %d, alphabet %s, %d letters" % (c["n"], c["alphabet"], c["length"]))
    print("sha256      %s" % c["sha256"].hex())
    print("family      %s" % c["family"])
    bad = check_generators(c)
    for gid, h in c["generators"]:
        ok = gid in GENERATORS and source_sha(gid) == h
        print("generator   %s %s %s" % (gid, h.hex(), "(matches this reader)" if ok else "(DOES NOT MATCH)"))
    print("payload     %d bytes xz" % len(c["payload"]))
    print("fallback    %s" % ("SPRA + xz, %d bytes" % len(c["fallback"]) if c["fallback_kind"] else "none"))
    parts = parse_payload(c["payload"])
    for name, data in parts.items():
        print("part        %-16s %9d bytes" % (name, len(data)))
    for line in parts["steps"].decode("ascii", "replace").rstrip("\n").split("\n"):
        print("step        %s" % line)
    if bad:
        print("refused     " + "; ".join(bad))
        return 2
    parse_steps(parts["steps"], parts, c["generators"])
    return 0


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0 if argv else 1
    cmd, a = argv[0], argv[1:]

    def opt(name, default=None):
        if name in a:
            i = a.index(name)
            v = a[i + 1]
            del a[i:i + 2]
            return v
        return default

    def flag(name):
        if name in a:
            a.remove(name)
            return True
        return False

    if cmd == "info":
        return info(a[0])
    if cmd == "decode":
        out, work = opt("-o"), opt("--work")
        keep, fb, quiet = flag("--keep"), flag("--fallback"), flag("--quiet")
        decode(a[0], out, work, keep, fb, quiet)
        return 0
    if cmd == "pack":
        specs = []
        while "--part" in a:
            specs.append(opt("--part"))
        out = a.pop(0)
        pack(out, int(opt("--n")), opt("--alphabet"), int(opt("--length")), opt("--sha256"), opt("--steps"),
             specs, opt("--fallback"))
        return 0
    if cmd == "add-fallback":
        c = read_container(a[0])
        with open(a[1], "rb") as f:
            c["fallback"] = f.read()
        with open(a[2], "wb") as f:
            f.write(build_container(c))
        return 0
    if cmd == "extract-fallback":
        c = read_container(a[0])
        if not c["fallback_kind"]:
            raise Refused("the container has no fallback")
        with open(a[1], "wb") as f:
            f.write(c["fallback"])
        return 0
    if cmd == "hashes":
        for gid in GENERATORS:
            print(gid, source_sha(gid).hex())
        return 0
    if cmd == "selpack":
        selpack(a[0], a[1], *(int(x) for x in a[2:4]))
        return 0
    if cmd == "seldiff":
        ro = flag("--rows-only")
        seldiff(a[0], a[1], a[2], ro)
        return 0
    if cmd == "transport":
        transport_file(a[0], int(a[1]), a[2])
        return 0
    if cmd == "planenc":
        planenc(a[0], a[1])
        return 0
    if cmd == "plandec":
        with open(a[0], "rb") as f, open(a[1], "wb") as g:
            g.write(plandec_bytes(f.read()))
        return 0
    say("unknown command %s" % cmd)
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Refused as e:
        say("recipe: refused:", e)
        sys.exit(2)
    except Mismatch as e:
        say("recipe: MISMATCH:", e)
        sys.exit(3)
    except StepFailed as e:
        say("recipe: step failed:", e)
        sys.exit(4)
    except (OSError, ValueError, IndexError) as e:
        say("recipe:", e)
        sys.exit(1)
