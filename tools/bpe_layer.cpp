// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jakub Halfar
// Distributed without warranty; see the repository LICENSE for full terms.

// An optional layer between SPRA 0.2 and xz (a proposal, not part of SPRA).
// Each run of equal logical tuples becomes one byte, and byte-pair encoding
// (Re-Pair stopped early, at most 255 pair rules) folds the commonest
// neighbours into new bytes before xz. See docs/bpe_layer.md for the format.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using Byte = std::uint8_t;
using Count = std::uint64_t;
constexpr std::size_t BUFFER_SIZE = 65536;
constexpr unsigned MAX_N = 32;
constexpr unsigned SYMBOLS = 256;
constexpr Count DEFAULT_MIN_COUNT = 1024;
constexpr std::array<Byte, 4> MAGIC{'S', 'P', 'B', 'E'};
constexpr std::array<Byte, 4> SPRA_MAGIC{'S', 'P', 'R', 'A'};
constexpr Byte VERSION = 1;
constexpr Byte SPRA_VERSION = 2;
constexpr const char* USAGE =
    "usage: spra-bpe encode [--min-count COUNT] [INPUT|-]\n"
    "       spra-bpe decode [--max-symbols COUNT] [INPUT|-]\n"
    "  encode reads SPRA 0.2 and writes SPBE; decode reads SPBE and writes SPRA 0.2.\n"
    "  --min-count: stop adding pair rules below COUNT occurrences (default 1024).\n"
    "  --max-symbols: refuse a header length above COUNT (default N * N!).\n"
    "  Omitted INPUT or '-' reads stdin; output goes to stdout. Compress with xz.\n";

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void binary_mode(std::FILE* file) {
#ifdef _WIN32
    if (_setmode(_fileno(file), _O_BINARY) == -1) fail("cannot set binary stream mode");
#else
    (void)file;
#endif
}

struct CloseFile {
    void operator()(std::FILE* file) const { if (file) std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, CloseFile>;

class Reader {
    std::FILE* file_;
    std::vector<Byte> buffer_ = std::vector<Byte>(BUFFER_SIZE);
    std::size_t position_ = 0, size_ = 0;
public:
    explicit Reader(std::FILE* file) : file_(file) {}
    int get() {
        if (position_ == size_) {
            size_ = std::fread(buffer_.data(), 1, buffer_.size(), file_);
            position_ = 0;
            if (size_ == 0) {
                if (std::ferror(file_)) fail("input read failed");
                return EOF;
            }
        }
        return buffer_[position_++];
    }
    Byte required(const char* field) {
        const int value = get();
        if (value == EOF) fail(std::string("truncated ") + field);
        return static_cast<Byte>(value);
    }
    // Unsigned LEB128: seven bits per byte, low bits first, high bit = more.
    Count varint(const char* field) {
        Count result = 0;
        for (unsigned shift = 0;; shift += 7) {
            const Byte byte = required(field);
            if (shift == 63 && byte > 1) fail(std::string("varint overflow in ") + field);
            result |= Count(byte & 0x7f) << shift;
            if (!(byte & 0x80)) return result;
        }
    }
};

class Writer {
    std::FILE* file_;
    std::vector<Byte> buffer_ = std::vector<Byte>(BUFFER_SIZE);
    std::size_t size_ = 0;
public:
    explicit Writer(std::FILE* file) : file_(file) {}
    void flush_buffer() {
        if (size_ && std::fwrite(buffer_.data(), 1, size_, file_) != size_) fail("output write failed");
        size_ = 0;
    }
    void put(Byte value) {
        if (size_ == buffer_.size()) flush_buffer();
        buffer_[size_++] = value;
    }
    void bytes(const Byte* data, std::size_t count) {
        while (count--) put(*data++);
    }
    void varint(Count value) {
        while (value >= 0x80) {
            put(static_cast<Byte>(value | 0x80));
            value >>= 7;
        }
        put(static_cast<Byte>(value));
    }
    void finish() {
        flush_buffer();
        if (std::fflush(file_) != 0) fail("output flush failed");
    }
};

struct Tuple {
    unsigned depth = 0;
    int offset = 0;
    bool operator==(const Tuple&) const = default;
};

struct Run {
    Tuple tuple;
    Count count = 0;
    bool operator==(const Run&) const = default;
};

struct RunHash {
    std::size_t operator()(const Run& run) const {
        return std::hash<Count>()((run.count << 12) ^ (Count(run.tuple.depth) << 6) ^ Count(run.tuple.offset & 63));
    }
};

// The SPRA header and seed, kept byte for byte.
struct Header {
    std::vector<Byte> bytes;
    unsigned bits = 4, n = 0;
    Count length = 0, tuple_count = 0;
    unsigned mask() const { return (1u << bits) - 1; }
};

Header read_spra_header(Reader& input) {
    Header header;
    for (Byte expected : SPRA_MAGIC)
        if (input.required("SPRA header") != expected) fail("not an SPRA 0.2 header");
    const Byte version = input.required("SPRA header");
    if (version != SPRA_VERSION) fail("SPRA version must be 0x02 (re-encode 0x01 files with reduced-codec)");
    const Byte layout = input.required("SPRA header");
    if (layout != 0x44 && layout != 0x66) fail("unsupported SPRA tuple layout");
    const Byte flags = input.required("SPRA header");
    if (flags & ~1u) fail("unsupported SPRA header flags");
    header.n = input.required("SPRA header");
    if (header.n == 0 || header.n > MAX_N) fail("SPRA header N must be 1..32");
    header.bits = layout & 15;
    header.bytes = {SPRA_MAGIC[0], SPRA_MAGIC[1], SPRA_MAGIC[2], SPRA_MAGIC[3], version, layout, flags,
                    static_cast<Byte>(header.n)};
    for (Count* field : {&header.length, &header.tuple_count}) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            const Byte byte = input.required("SPRA header");
            *field |= Count(byte) << shift;
            header.bytes.push_back(byte);
        }
    }
    const auto seed_length = static_cast<unsigned>(std::min<Count>(header.length, header.n - 1));
    if (header.tuple_count > header.length - seed_length) fail("tuple count exceeds decoded length");
    for (unsigned i = 0; i < seed_length; ++i) {
        const Byte symbol = input.required("seed");
        if (symbol >= header.n) fail("seed symbol is outside header alphabet");
        header.bytes.push_back(symbol);
    }
    return header;
}

// One pass of byte-pair encoding: the most frequent adjacent pair (a run of
// equal bytes counts its pairs without overlap; ties go to the smaller pair
// left * 256 + right), or nothing when it occurs fewer than min_count times.
std::optional<std::pair<Byte, Byte>> commonest_pair(const std::vector<Byte>& body, Count min_count) {
    std::vector<Count> counts(SYMBOLS * SYMBOLS);
    for (std::size_t i = 0; i + 1 < body.size(); ++i) {
        ++counts[body[i] * SYMBOLS + body[i + 1]];
        if (body[i] == body[i + 1] && i + 2 < body.size() && body[i + 2] == body[i]) ++i;
    }
    const auto best = std::max_element(counts.begin(), counts.end()) - counts.begin();
    if (counts[best] < min_count || counts[best] == 0) return std::nullopt;
    return std::pair{static_cast<Byte>(best / SYMBOLS), static_cast<Byte>(best % SYMBOLS)};
}

// Replace the pair left to right by symbol.
void replace_pair(std::vector<Byte>& body, std::pair<Byte, Byte> pair, Byte symbol) {
    std::size_t write = 0;
    for (std::size_t i = 0; i < body.size();) {
        if (i + 1 < body.size() && body[i] == pair.first && body[i + 1] == pair.second) {
            body[write++] = symbol;
            i += 2;
        } else body[write++] = body[i++];
    }
    body.resize(write);
}

void encode(std::FILE* source, Count min_count) {
    Reader input(source);
    const Header header = read_spra_header(input);
    // Logical tuples as maximal runs; run ids in order of first occurrence.
    std::unordered_map<Run, std::uint32_t, RunHash> ids;
    std::vector<Run> runs;
    std::vector<Count> counts;
    std::vector<Byte> body;
    Run current{};
    const auto close_run = [&] {
        if (!current.count) return;
        const auto [it, added] = ids.try_emplace(current, static_cast<std::uint32_t>(runs.size()));
        if (added) {
            if (runs.size() == SYMBOLS)
                fail("more than 256 distinct runs of equal tuples; keep this word as SPRA plus xz");
            runs.push_back(current);
            counts.push_back(0);
        }
        ++counts[it->second];
        body.push_back(static_cast<Byte>(it->second));
    };
    const unsigned width = 2 * header.bits, mask = header.mask();
    unsigned pending = 0, have = 0;
    Tuple previous{};
    bool has_previous = false;
    for (Count expanded = 0; expanded < header.tuple_count;) {
        while (have < width) {
            pending = (pending << 8) | input.required("tuple body");
            have += 8;
        }
        have -= width;
        const unsigned token = pending >> have;
        pending &= (1u << have) - 1;
        const unsigned low = token & mask;
        const Tuple tuple{token >> header.bits, low <= mask / 2 ? int(low) : int(low) - int(mask + 1)};
        Count repeat = 1;
        if (tuple.offset == 0 && tuple.depth > 0) {
            if (tuple.depth == mask) fail("reserved tuple extension opcode");
            if (!has_previous) fail("repeat marker has no preceding tuple");
            if (tuple.depth > header.tuple_count - expanded) fail("repeat marker exceeds logical tuple count");
            repeat = tuple.depth;
        } else {
            previous = tuple;
            has_previous = true;
        }
        if (current.count && current.tuple == previous) current.count += repeat;
        else {
            close_run();
            current = {previous, repeat};
        }
        expanded += repeat;
    }
    close_run();
    if (pending) fail("nonzero padding bits after final tuple");
    if (input.get() != EOF) fail("extra bytes after tuple body");

    // Number the runs by decreasing count, ties by first occurrence.
    std::vector<std::uint32_t> order(runs.size());
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return counts[a] > counts[b]; });
    std::array<Byte, SYMBOLS> rank{};
    for (std::uint32_t r = 0; r < order.size(); ++r) rank[order[r]] = static_cast<Byte>(r);
    for (auto& symbol : body) symbol = rank[symbol];
    std::vector<std::pair<Byte, Byte>> rules;
    while (runs.size() + rules.size() < SYMBOLS) {
        const auto pair = commonest_pair(body, min_count);
        if (!pair) break;
        replace_pair(body, *pair, static_cast<Byte>(runs.size() + rules.size()));
        rules.push_back(*pair);
    }

    Writer output(stdout);
    output.bytes(MAGIC.data(), MAGIC.size());
    output.put(VERSION);
    output.bytes(header.bytes.data(), header.bytes.size());
    output.varint(runs.size());
    for (auto r : order) {
        output.put(static_cast<Byte>(runs[r].tuple.depth));
        output.put(static_cast<Byte>(runs[r].tuple.offset));
        output.varint(runs[r].count);
    }
    output.put(static_cast<Byte>(rules.size()));
    for (auto [left, right] : rules) {
        output.put(left);
        output.put(right);
    }
    output.varint(body.size());
    output.bytes(body.data(), body.size());
    output.finish();
}

// Canonical SPRA 0.2 body: a literal per run of equal tuples, then markers of
// M-1 repeats, then the remainder (what reduced-codec encode writes).
class SpraBodyWriter {
    Writer& output_;
    unsigned bits_, pending_ = 0, have_ = 0, repeats_ = 0;
    Tuple previous_{};
    bool has_previous_ = false;
    void token(unsigned value) {
        pending_ = (pending_ << (2 * bits_)) | value;
        have_ += 2 * bits_;
        while (have_ >= 8) {
            have_ -= 8;
            output_.put(static_cast<Byte>(pending_ >> have_));
        }
        pending_ &= (1u << have_) - 1;
    }
    void flush_repeats() {
        if (repeats_) token(repeats_ << bits_);
        repeats_ = 0;
    }
public:
    SpraBodyWriter(Writer& output, unsigned bits) : output_(output), bits_(bits) {}
    void put(Tuple tuple, Count count) {
        const unsigned mask = (1u << bits_) - 1;
        if (!(has_previous_ && tuple == previous_)) {
            flush_repeats();
            token((tuple.depth << bits_) | (static_cast<unsigned>(tuple.offset) & mask));
            previous_ = tuple;
            has_previous_ = true;
            --count;
        }
        while (count) {
            const auto take = static_cast<unsigned>(std::min<Count>(count, mask - 1 - repeats_));
            repeats_ += take;
            count -= take;
            if (repeats_ == mask - 1) flush_repeats();
        }
    }
    void finish() {
        flush_repeats();
        if (have_) output_.put(static_cast<Byte>(pending_ << (8 - have_)));
    }
};

Count default_max_symbols(unsigned n) {
    Count product = n;
    for (unsigned factor = 2; factor <= n; ++factor) {
        if (product > std::numeric_limits<Count>::max() / factor) return std::numeric_limits<Count>::max();
        product *= factor;
    }
    return product;
}

void decode(std::FILE* source, std::optional<Count> max_symbols = std::nullopt) {
    Reader input(source);
    for (Byte expected : MAGIC)
        if (input.required("header") != expected) fail("not an SPBE file");
    if (input.required("header") != VERSION) fail("unsupported SPBE version");
    const Header header = read_spra_header(input);
    const Count limit = max_symbols ? *max_symbols : default_max_symbols(header.n);
    if (header.length > limit)
        fail("header length " + std::to_string(header.length) + " exceeds " +
             (max_symbols ? "--max-symbols " : "N * N! = ") + std::to_string(limit));
    const Count run_count = input.varint("run table");
    if (run_count > SYMBOLS) fail("more than 256 runs");
    if (run_count > header.tuple_count) fail("more distinct runs than logical tuples");
    std::vector<Run> runs(run_count);
    const unsigned mask = header.mask();
    const int low = -int(mask + 1) / 2, high = int(mask) / 2;
    for (auto& run : runs) {
        run.tuple.depth = input.required("run table");
        run.tuple.offset = static_cast<std::int8_t>(input.required("run table"));
        run.count = input.varint("run table");
        if (run.tuple.depth > mask || run.tuple.offset < low || run.tuple.offset > high)
            fail("run tuple does not fit the SPRA layout");
        if (run.tuple.offset == 0 && run.tuple.depth > 0) fail("run tuple has positive depth and offset 0");
        if (run.count == 0 || run.count > header.tuple_count) fail("run count must be 1..tuple count");
    }
    const unsigned rule_count = input.required("rule table");
    if (run_count + rule_count > SYMBOLS) fail("more than 256 runs and rules");
    std::vector<std::pair<Byte, Byte>> rules(rule_count);
    for (unsigned k = 0; k < rule_count; ++k) {
        rules[k].first = input.required("rule table");
        rules[k].second = input.required("rule table");
        if (rules[k].first >= run_count + k || rules[k].second >= run_count + k)
            fail("pair rule refers to a later symbol");
    }
    // Expand every body byte down to runs and write the SPRA file.
    Writer output(stdout);
    output.bytes(header.bytes.data(), header.bytes.size());
    SpraBodyWriter body(output, header.bits);
    Count expanded = 0;
    const auto expand = [&](auto& self, unsigned symbol) -> void {
        if (symbol >= run_count) {
            self(self, rules[symbol - run_count].first);
            self(self, rules[symbol - run_count].second);
            return;
        }
        const Run& run = runs[symbol];
        if (run.count > header.tuple_count - expanded) fail("runs exceed the logical tuple count");
        expanded += run.count;
        body.put(run.tuple, run.count);
    };
    const Count body_length = input.varint("body");
    for (Count i = 0; i < body_length; ++i) {
        const Byte symbol = input.required("body");
        if (symbol >= run_count + rule_count) fail("body byte is neither a run nor a rule");
        expand(expand, symbol);
    }
    if (expanded != header.tuple_count) fail("runs do not reach the logical tuple count");
    if (input.get() != EOF) fail("extra bytes after body");
    body.finish();
    output.finish();
}

struct Options {
    bool encode = false;
    Count min_count = DEFAULT_MIN_COUNT;
    std::optional<Count> max_symbols;
    std::string input = "-";
};

Count count_value(int argc, char** argv, int& i, std::string_view name) {
    const std::string_view arg = argv[i];
    std::string_view value;
    if (arg == name) {
        if (++i == argc) fail(std::string(name) + " requires a count");
        value = argv[i];
    } else value = arg.substr(name.size() + 1);
    Count count = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        fail(std::string(name) + " requires a count");
    return count;
}

Options options(int argc, char** argv) {
    if (argc < 2) fail(USAGE);
    const std::string_view command = argv[1];
    if (command != "encode" && command != "decode") fail(USAGE);
    Options result;
    result.encode = command == "encode";
    bool has_input = false, has_min_count = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (result.encode && (arg == "--min-count" || arg.starts_with("--min-count="))) {
            if (has_min_count) fail("duplicate --min-count option");
            has_min_count = true;
            result.min_count = count_value(argc, argv, i, "--min-count");
        } else if (!result.encode && (arg == "--max-symbols" || arg.starts_with("--max-symbols="))) {
            if (result.max_symbols) fail("duplicate --max-symbols option");
            result.max_symbols = count_value(argc, argv, i, "--max-symbols");
        } else if (!has_input && (arg == "-" || !arg.starts_with('-'))) {
            result.input = arg;
            has_input = true;
        } else {
            fail("unexpected argument: " + std::string(arg));
        }
    }
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        if (std::fputs(USAGE, stdout) == EOF || std::fflush(stdout) != 0) fail("output write failed");
        return 0;
    }
    const auto args = options(argc, argv);
    File file;
    std::FILE* source = stdin;
    if (args.input != "-") {
        file.reset(std::fopen(args.input.c_str(), "rb"));
        if (!file) fail("cannot open input: " + args.input);
        source = file.get();
    }
    binary_mode(source);
    binary_mode(stdout);
    if (args.encode) encode(source, args.min_count);
    else decode(source, args.max_symbols);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "spra-bpe: %s\n", error.what());
    return 1;
}
