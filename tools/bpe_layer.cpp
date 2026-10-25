// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jakub Halfar
// Copyright (C) 2026 Theo H.
// Distributed without warranty; see the repository LICENSE for full terms.

// Halfar's optional layer between SPRA 0.2 and xz, integrated from PR #2.
// SPBE 2 uses bounded-prefix learning and
// incremental fixed-rule substitution. See docs/bpe_streaming.md.
// Each run of equal logical tuples becomes one byte, and byte-pair encoding
// (Re-Pair stopped early, at most 255 pair rules) folds the commonest
// neighbours into new bytes before xz. See docs/bpe_layer.md for the format.
#include <algorithm>
#include <bit>
#include <functional>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cerrno>
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
#else
#include <unistd.h>
#endif

namespace {
using Byte = std::uint8_t;
using Count = std::uint64_t;
constexpr std::size_t BUFFER_SIZE = 65536;
constexpr unsigned MAX_N = 32;
constexpr unsigned SYMBOLS = 256;
constexpr std::array<Byte, 4> MAGIC{'S', 'P', 'B', 'E'};
constexpr std::array<Byte, 4> SPRA_MAGIC{'S', 'P', 'R', 'A'};
constexpr Byte SPRA_VERSION = 2;
constexpr const char* USAGE =
    "usage: spra-bpe encode [--min-count COUNT] [--train-runs COUNT | --whole-file] [INPUT|-]\n"
    "       spra-bpe decode [--max-symbols COUNT] [INPUT|-]\n"
    "  encode reads SPRA 0.2 and writes SPBE; decode reads SPBE and writes SPRA 0.2.\n"
    "  Default: streaming SPBE 2, train on up to 65536 runs, min-count 16.\n"
    "  --train-runs: training prefix limit, 1..1048576; one fixed dictionary per file.\n"
    "  --whole-file: buffer all runs and write SPBE 1 (default min-count 1024).\n"
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
            // One OS read returns available pipe bytes instead of waiting to
            // fill an entire stdio buffer while the producer is still live.
            int received;
            do {
#ifdef _WIN32
                received = _read(_fileno(file_), buffer_.data(), static_cast<unsigned>(buffer_.size()));
#else
                received = static_cast<int>(::read(fileno(file_), buffer_.data(), buffer_.size()));
#endif
            } while (received < 0 && errno == EINTR);
            if (received < 0) fail("input read failed");
            size_ = static_cast<std::size_t>(received);
            position_ = 0;
            if (size_ == 0) {
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

// Parsing is one pass. Only the current maximal run is retained here.
// Streaming mode splits exceptional long runs, bounding the learning delay;
// the decoder coalesces the pieces back to the original canonical SPRA bytes.
template<class Emit>
void read_runs(Reader& input, const Header& header, Emit emit, Count split_at = 0) {
    Run current{};
    const auto close_run = [&] { if (current.count) emit(current); };
    const unsigned width = 2 * header.bits, mask = header.mask();
    unsigned pending = 0, have = 0;
    Tuple previous{};
    bool has_previous = false;
    for (Count expanded = 0; expanded < header.tuple_count;) {
        while (have < width) { pending = (pending << 8) | input.required("tuple body"); have += 8; }
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
        } else { previous = tuple; has_previous = true; }
        if (current.count && current.tuple == previous) current.count += repeat;
        else { close_run(); current = {previous, repeat}; }
        if (split_at && current.count >= split_at) {
            const auto remaining = current.count - split_at;
            current.count = split_at; close_run(); current = {previous, remaining};
        }
        expanded += repeat;
    }
    close_run();
    if (pending) fail("nonzero padding bits after final tuple");
    if (input.get() != EOF) fail("extra bytes after tuple body");
}

// Halfar's frequency ordering and early-stopped Re-Pair, unchanged, applied
// to the training prefix or whole file. Input is one byte per run.
void write_frame(Writer& output, const std::vector<Run>& runs,
                 const std::vector<Count>& counts, std::vector<Byte>& body, Count min_count,
                 std::array<Byte, SYMBOLS>* learned_rank = nullptr,
                 std::vector<std::pair<Byte, Byte>>* learned_rules = nullptr) {
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
    output.varint(runs.size());
    for (auto r : order) {
        output.put(static_cast<Byte>(runs[r].tuple.depth));
        output.put(static_cast<Byte>(runs[r].tuple.offset));
        output.varint(runs[r].count);
    }
    output.put(static_cast<Byte>(rules.size()));
    for (auto [left, right] : rules) { output.put(left); output.put(right); }
    output.varint(body.size());
    output.bytes(body.data(), body.size());
    if (learned_rank) *learned_rank = rank;
    if (learned_rules) *learned_rules = rules;
}

constexpr Count DEFAULT_TRAIN_RUNS = 65536;
constexpr Count MAX_TRAIN_RUNS = 1048576;

// A cascade of Halfar's fixed pair substitutions. Each stage retains at most
// its left byte; a bitmap skips rules which cannot act on the current byte.
class PairStream {
    const std::vector<std::pair<Byte, Byte>>& rules_;
    unsigned base_;
    std::array<std::array<std::uint64_t, 4>, SYMBOLS> left_{};
    std::array<std::uint64_t, 4> pending_{};
    std::function<void(Byte)> emit_;
public:
    PairStream(const std::vector<std::pair<Byte, Byte>>& rules, unsigned base, std::function<void(Byte)> emit)
        : rules_(rules), base_(base), emit_(std::move(emit)) {
        for (unsigned i = 0; i < rules.size(); ++i) left_[rules[i].first][i / 64] |= std::uint64_t(1) << (i % 64);
    }
    void put(Byte symbol, unsigned start = 0) {
        for (;;) {
            unsigned stage = static_cast<unsigned>(rules_.size());
            for (unsigned word = start / 64; word < 4; ++word) {
                auto candidates = left_[symbol][word] | pending_[word];
                if (word == start / 64) candidates &= ~std::uint64_t(0) << (start % 64);
                if (candidates) { stage = word * 64 + std::countr_zero(candidates); break; }
            }
            if (stage >= rules_.size()) { emit_(symbol); return; }
            auto& pending = pending_[stage / 64]; const auto bit = std::uint64_t(1) << (stage % 64);
            if (!(pending & bit)) { pending |= bit; return; }
            pending &= ~bit;
            if (symbol == rules_[stage].second) { symbol = static_cast<Byte>(base_ + stage); start = stage + 1; }
            else {
                put(rules_[stage].first, stage + 1);
                if (symbol == rules_[stage].first) { pending |= bit; return; }
                start = stage + 1;
            }
        }
    }
    void finish() {
        for (unsigned stage = 0; stage < rules_.size(); ++stage) {
            const auto bit = std::uint64_t(1) << (stage % 64);
            if (pending_[stage / 64] & bit) { pending_[stage / 64] &= ~bit; put(rules_[stage].first, stage + 1); }
        }
    }
};

void encode(std::FILE* source, Count min_count, Count train_runs = DEFAULT_TRAIN_RUNS, bool whole_file = false) {
    Reader input(source); const Header header = read_spra_header(input);
    std::unordered_map<Run, std::uint32_t, RunHash> ids;
    std::vector<Run> runs; std::vector<Count> counts; std::vector<Byte> body;
    if (!whole_file) body.reserve(static_cast<std::size_t>(train_runs));
    Writer output(stdout); bool trained = false; unsigned next_symbol = 0;
    std::vector<std::pair<Byte, Byte>> rules;
    std::unique_ptr<PairStream> stream;
    std::vector<Byte> data; data.reserve(4096);
    const auto flush_data = [&] {
        if (data.empty()) return;
        output.put(1); output.varint(data.size()); output.bytes(data.data(), data.size());
        output.finish(); data.clear();
    };
    const auto preamble = [&] {
        output.bytes(MAGIC.data(), MAGIC.size()); output.put(whole_file ? 1 : 2);
        output.bytes(header.bytes.data(), header.bytes.size());
    };
    if (!whole_file) { preamble(); output.finish(); }
    const auto train = [&] {
        std::array<Byte, SYMBOLS> rank{};
        write_frame(output, runs, counts, body, min_count, &rank, &rules);
        for (auto& [run, id] : ids) id = rank[id];
        next_symbol = static_cast<unsigned>(runs.size() + rules.size());
        stream = std::make_unique<PairStream>(rules, static_cast<unsigned>(runs.size()), [&](Byte symbol) {
            data.push_back(symbol); if (data.size() == 4096) flush_data();
        });
        body.clear(); body.shrink_to_fit(); counts.clear(); runs.clear();
        trained = true; output.finish();
    };
    Count bootstrap_tuples = 0;
    read_runs(input, header, [&](const Run& run) {
        auto found = ids.find(run);
        if (!whole_file && !trained && (body.size() == train_runs || bootstrap_tuples >= train_runs * 16
                || (found == ids.end() && runs.size() == SYMBOLS))) train();
        found = ids.find(run);
        if (trained) {
            if (found == ids.end()) {
                stream->finish(); flush_data();
                output.put(next_symbol < SYMBOLS ? 2 : 3);
                output.put(static_cast<Byte>(run.tuple.depth)); output.put(static_cast<Byte>(run.tuple.offset)); output.varint(run.count);
                if (next_symbol < SYMBOLS) { found = ids.emplace(run, next_symbol++).first; }
                else { output.finish(); return; }
            }
            stream->put(static_cast<Byte>(found->second));
            if (run.count >= 65536) { stream->finish(); flush_data(); }
        } else {
            if (found == ids.end()) {
                if (runs.size() == SYMBOLS) fail("more than 256 distinct runs; use streaming mode or SPRA plus xz");
                const auto id = static_cast<std::uint32_t>(runs.size()); ids.emplace(run,id); runs.push_back(run); counts.push_back(1); body.push_back(static_cast<Byte>(id));
            } else { ++counts[found->second]; body.push_back(static_cast<Byte>(found->second)); }
            bootstrap_tuples += run.count;
        }
    }, whole_file ? 0 : 65536);
    if (whole_file) { preamble(); write_frame(output, runs, counts, body, min_count); }
    else { if (!trained) train(); stream->finish(); flush_data(); output.put(0); }
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
    for (Byte expected : MAGIC) if (input.required("header") != expected) fail("not an SPBE file");
    const unsigned version = input.required("header");
    if (version != 1 && version != 2) fail("unsupported SPBE version");
    const Header header = read_spra_header(input);
    const Count limit = max_symbols ? *max_symbols : default_max_symbols(header.n);
    if (header.length > limit) fail("header length " + std::to_string(header.length) + " exceeds " + (max_symbols ? "--max-symbols " : "N * N! = ") + std::to_string(limit));
    const unsigned mask = header.mask();
    const auto read_run = [&] {
        Run run; run.tuple.depth = input.required("run table");
        run.tuple.offset = static_cast<std::int8_t>(input.required("run table")); run.count = input.varint("run table");
        if (run.tuple.depth > mask || run.tuple.offset < -int(mask + 1)/2 || run.tuple.offset > int(mask)/2) fail("run tuple does not fit the SPRA layout");
        if (run.tuple.offset == 0 && run.tuple.depth > 0) fail("run tuple has positive depth and offset 0");
        if (run.count == 0 || run.count > header.tuple_count) fail("run count must be 1..tuple count");
        return run;
    };
    struct Symbol { bool is_run = false; Run run; Byte left = 0, right = 0; };
    std::array<Symbol,SYMBOLS> symbols{};
    const Count run_count = input.varint("run table");
    if (run_count > SYMBOLS) fail("more than 256 runs");
    if (run_count > header.tuple_count) fail("more distinct runs than logical tuples");
    for (unsigned i = 0; i < run_count; ++i) { symbols[i].is_run = true; symbols[i].run = read_run(); }
    const unsigned rule_count = input.required("rule table");
    if (run_count + rule_count > SYMBOLS) fail("more than 256 runs and rules");
    unsigned used = static_cast<unsigned>(run_count);
    for (unsigned i = 0; i < rule_count; ++i) {
        auto& rule = symbols[used]; rule.left = input.required("rule table"); rule.right = input.required("rule table");
        if (rule.left >= used || rule.right >= used) fail("pair rule refers to a later symbol");
        ++used;
    }
    Writer output(stdout); output.bytes(header.bytes.data(),header.bytes.size());
    SpraBodyWriter body(output,header.bits); Count expanded = 0;
    const auto emit_run = [&](const Run& run) {
        if (run.count > header.tuple_count - expanded) fail("runs exceed the logical tuple count");
        expanded += run.count; body.put(run.tuple,run.count);
    };
    const auto expand = [&](auto& self,unsigned symbol) -> void {
        if (symbol >= used) fail("body byte is neither a run nor a rule");
        const auto& entry = symbols[symbol];
        if (entry.is_run) emit_run(entry.run);
        else { self(self,entry.left); self(self,entry.right); }
    };
    const auto read_body = [&](bool initial) {
        const Count size = input.varint("body");
        if (version == 2 && ((!initial && size == 0) || size > MAX_TRAIN_RUNS)) fail("invalid streaming body size");
        for (Count i = 0; i < size; ++i) expand(expand,input.required("body"));
        if (version == 2) output.finish();
    };
    read_body(true);
    if (version == 2) for (;;) {
        const auto tag = input.required("record tag");
        if (tag == 0) break;
        if (tag == 1) read_body(false);
        else if (tag == 2) {
            if (used == SYMBOLS) fail("more than 256 symbols");
            symbols[used].is_run = true; symbols[used++].run = read_run();
        } else if (tag == 3) { emit_run(read_run()); output.finish(); }
        else fail("unsupported streaming record tag");
    }
    if (expanded != header.tuple_count) fail("runs do not reach the logical tuple count");
    if (input.get() != EOF) fail("extra bytes after body");
    body.finish(); output.finish();
}
struct Options {
    bool encode = false;
    bool whole_file = false;
    Count min_count = 16, train_runs = DEFAULT_TRAIN_RUNS;
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
    bool has_input = false, has_min_count = false, has_train = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (result.encode && (arg == "--min-count" || arg.starts_with("--min-count="))) {
            if (has_min_count) fail("duplicate --min-count option");
            has_min_count = true;
            result.min_count = count_value(argc, argv, i, "--min-count");
        } else if (result.encode && (arg == "--train-runs" || arg.starts_with("--train-runs="))) {
            if (has_train) fail("duplicate --train-runs option");
            has_train = true;
            result.train_runs = count_value(argc, argv, i, "--train-runs");
            if (!result.train_runs || result.train_runs > MAX_TRAIN_RUNS) fail("--train-runs must be 1..1048576");
        } else if (result.encode && arg == "--whole-file") {
            if (result.whole_file) fail("duplicate --whole-file option");
            result.whole_file = true;
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
    if (result.whole_file && has_train) fail("--whole-file and --train-runs cannot be combined");
    if (result.whole_file && !has_min_count) result.min_count = 1024;
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
    if (args.encode) encode(source, args.min_count, args.train_runs, args.whole_file);
    else decode(source, args.max_symbols);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "spra-bpe: %s\n", error.what());
    return 1;
}
