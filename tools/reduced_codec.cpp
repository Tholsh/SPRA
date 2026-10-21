// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Theo H.
// Distributed without warranty; see the repository LICENSE for full terms.

// A strict, configurable-width tuple preconditioner for xz. See
// docs/reduced_binary.md for the wire format and its representability limits.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
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
constexpr unsigned MAX_OFFSET_REACH = 32;
constexpr std::array<Byte, 4> MAGIC{'S', 'P', 'R', 'A'};
constexpr Byte VERSION = 2; // SPRA 0.2: mandatory tuple RLE.
constexpr std::string_view ONE_BASED = "123456789ABCDEFGHIJKLMNOPQRSTUVW";
constexpr std::string_view ZERO_BASED = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
static_assert(ONE_BASED.size() == MAX_N && ZERO_BASED.size() == MAX_N);
constexpr const char* USAGE =
    "usage: reduced-codec encode [--bits 4|6] [-z|--zero-based] N [INPUT|-]\n"
    "       reduced-codec decode [INPUT|-]\n"
    "  N: 1..32; omitted INPUT or '-' reads stdin.\n"
    "  --bits: bits per field (default 4); tuples use 8 or 12 packed bits.\n"
    "  encode writes SPRA 0.2 with tuple RLE; decode also reads 0.1 files.\n"
    "  decode detects the layout from the header.\n"
    "  encode writes binary to stdout; decode writes symbols without a newline.\n"
    "  Errors go to stderr. Redirect stdout to save the result.\n";

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

// Keep field ranges and wire identifiers together. Packing and word semantics
// use this description rather than separate implementations for each layout.
struct Layout {
    unsigned bits;
    explicit Layout(unsigned width) : bits(width) {
        if (bits != 4 && bits != 6) fail("--bits must be 4 or 6");
    }
    unsigned mask() const { return (1u << bits) - 1; }
    unsigned sign() const { return 1u << (bits - 1); }
    Byte id() const { return static_cast<Byte>((bits << 4) | bits); }
    static Layout from_id(Byte id) {
        if (id != 0x44 && id != 0x66) fail("unsupported tuple layout (expected 0x44 or 0x66)");
        return Layout(id & 15);
    }
};

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
    std::array<Byte, BUFFER_SIZE> buffer_{};
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
    Count little_u64(const char* field) {
        Count result = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            result |= Count(required(field)) << shift;
        return result;
    }
};

class Writer {
    std::FILE* file_;
    std::array<Byte, BUFFER_SIZE> buffer_{};
    std::size_t size_ = 0;
public:
    explicit Writer(std::FILE* file) : file_(file) {}
    void flush_buffer() {
        if (size_ && std::fwrite(buffer_.data(), 1, size_, file_) != size_)
            fail("output write failed");
        size_ = 0;
    }
    void put(Byte value) {
        if (size_ == buffer_.size()) flush_buffer();
        buffer_[size_++] = value;
    }
    void bytes(const Byte* data, std::size_t count) {
        while (count) {
            if (size_ == buffer_.size()) flush_buffer();
            const auto take = std::min(count, buffer_.size() - size_);
            std::copy_n(data, take, buffer_.data() + size_);
            size_ += take;
            data += take;
            count -= take;
        }
    }
    void little_u64(Count value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            put(static_cast<Byte>((value >> shift) & 0xff));
    }
    void finish() {
        flush_buffer();
        if (std::fflush(file_) != 0) fail("output flush failed");
    }
};

// Tuple bits are most-significant first, with zero padding to the final byte.
// Four-bit tokens occupy one byte; two six-bit tokens occupy three bytes.
class TupleWriter {
    Writer& output_;
    Layout layout_;
    unsigned pending_ = 0, count_ = 0;
public:
    TupleWriter(Writer& output, Layout layout) : output_(output), layout_(layout) {}
    void put(unsigned depth, int offset) {
        const unsigned tuple = (depth << layout_.bits) | (static_cast<unsigned>(offset) & layout_.mask());
        pending_ = (pending_ << (2 * layout_.bits)) | tuple;
        count_ += 2 * layout_.bits;
        while (count_ >= 8) {
            count_ -= 8;
            output_.put(static_cast<Byte>(pending_ >> count_));
        }
        pending_ &= (1u << count_) - 1;
    }
    void finish() {
        if (count_) output_.put(static_cast<Byte>(pending_ << (8 - count_)));
        pending_ = count_ = 0;
    }
};

struct Tuple {
    unsigned depth = 0;
    int offset = 0;
    bool operator==(const Tuple&) const = default;
};

class TupleReader {
    Reader& input_;
    Layout layout_;
    unsigned pending_ = 0, count_ = 0;
public:
    TupleReader(Reader& input, Layout layout) : input_(input), layout_(layout) {}
    Tuple get() {
        const unsigned width = 2 * layout_.bits;
        while (count_ < width) {
            pending_ = (pending_ << 8) | input_.required("tuple body");
            count_ += 8;
        }
        count_ -= width;
        const unsigned tuple = pending_ >> count_;
        pending_ &= (1u << count_) - 1;
        const int low = static_cast<int>(tuple & layout_.mask());
        const int offset = low < static_cast<int>(layout_.sign()) ? low : low - (1 << layout_.bits);
        return {tuple >> layout_.bits, offset};
    }
    void finish() const {
        if (pending_) fail("nonzero padding bits after final tuple");
    }
};

// Positive depth with offset zero would continue a clean window, so it is
// impossible as an ordinary dirty tuple. Use it to repeat the last literal.
// The maximal depth/zero-offset token is reserved for future extensions.
class RleTupleWriter {
    TupleWriter tokens_;
    Layout layout_;
    Tuple previous_{};
    bool has_previous_ = false;
    unsigned repeats_ = 0;
    void flush_repeats() {
        if (repeats_) tokens_.put(repeats_, 0);
        repeats_ = 0;
    }
public:
    RleTupleWriter(Writer& output, Layout layout) : tokens_(output, layout), layout_(layout) {}
    void put(unsigned depth, int offset) {
        const Tuple tuple{depth, offset};
        if (has_previous_ && tuple == previous_) {
            if (++repeats_ == layout_.mask() - 1) flush_repeats();
        } else {
            flush_repeats();
            tokens_.put(depth, offset);
            previous_ = tuple;
            has_previous_ = true;
        }
    }
    void finish() {
        flush_repeats();
        tokens_.finish();
    }
};

// A six-bit reference reaches at most N+32 symbols into the past. The longest
// distinct suffix detects clean windows without an N!-sized coverage bitmap.
class WordState {
    unsigned n_;
    unsigned suffix_ = 0;
    std::array<Byte, MAX_N + MAX_OFFSET_REACH> history_{};
    std::array<Count, MAX_N> last_{};
public:
    Count length = 0;
    explicit WordState(unsigned n) : n_(n) {}
    Count previous(Byte symbol) const { return last_[symbol]; }
    bool clean() const { return length >= n_ && suffix_ == n_; }
    void append(Byte symbol) {
        if (length == std::numeric_limits<Count>::max()) fail("word exceeds uint64 length");
        const Count position = length + 1;
        const Count distance = last_[symbol] ? position - last_[symbol] : position;
        suffix_ = static_cast<unsigned>(std::min<Count>({n_, suffix_ + 1, distance}));
        history_[length % history_.size()] = symbol;
        last_[symbol] = position;
        length = position;
    }
    Byte back(unsigned distance) const {
        if (distance == 0 || distance > length || distance > history_.size())
            fail("copy reference precedes available history at symbol " + std::to_string(length + 1));
        return history_[(length - distance) % history_.size()];
    }
    Byte next_clean() const {
        // Once a window is clean, further clean steps are distance-N copies.
        if (clean()) return back(n_);
        if (suffix_ < n_ - 1) fail("clean run follows a repeated suffix at symbol " + std::to_string(length + 1));
        std::uint32_t used = 0;
        for (unsigned distance = 1; distance < n_; ++distance)
            used |= std::uint32_t{1} << back(distance);
        for (unsigned symbol = 0; symbol < n_; ++symbol)
            if (!(used & (std::uint32_t{1} << symbol))) return static_cast<Byte>(symbol);
        fail("clean window has no missing symbol");
    }
};

bool whitespace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

void encode(std::FILE* source, unsigned n, bool zero_based, Layout layout) {
    File spool(std::tmpfile());
    if (!spool) fail("cannot create temporary tuple file");
    binary_mode(spool.get());
    Writer body(spool.get());
    RleTupleWriter tuples(body, layout);
    Reader input(source);
    WordState state(n);
    std::vector<Byte> seed;
    seed.reserve(n - 1);
    Count clean_run = 0, tuple_count = 0;
    const auto alphabet = zero_based ? ZERO_BASED : ONE_BASED;
    for (int c = input.get(); c != EOF; c = input.get()) {
        if (whitespace(c)) continue;
        const auto label = alphabet.find(static_cast<char>(c));
        if (label == std::string_view::npos || label >= n)
            fail("invalid alphabet symbol at symbol " + std::to_string(state.length + 1));
        const Byte symbol = static_cast<Byte>(label);
        const Count previous = state.previous(symbol);
        state.append(symbol);
        if (state.length < n) {
            seed.push_back(symbol);
            continue;
        }
        if (state.clean()) {
            ++clean_run;
            continue;
        }
        const auto location = " at symbol " + std::to_string(state.length);
        if (clean_run > layout.mask())
            fail("depth " + std::to_string(clean_run) + " exceeds " + std::to_string(layout.mask()) + location);
        if (previous == 0)
            fail("dirty symbol has no earlier occurrence" + location + "; copy-only layouts cannot encode it");
        const Count distance = state.length - previous;
        const unsigned minimum_distance = n >= layout.sign() ? n - (layout.sign() - 1) : 1;
        const unsigned maximum_distance = n + layout.sign();
        if (distance < minimum_distance || distance > maximum_distance)
            fail("copy distance " + std::to_string(distance) + " is outside " + std::to_string(layout.bits) +
                 "-bit range " + std::to_string(minimum_distance) + ".." + std::to_string(maximum_distance) + location);
        const int offset = static_cast<int>(n) - static_cast<int>(distance);
        tuples.put(static_cast<unsigned>(clean_run), offset);
        ++tuple_count;
        clean_run = 0;
    }
    tuples.finish();
    body.finish();
    if (std::fseek(spool.get(), 0, SEEK_SET) != 0) fail("cannot rewind temporary tuple file");

    // Publish only after the whole input and all field ranges are validated.
    Writer output(stdout);
    output.bytes(MAGIC.data(), MAGIC.size());
    output.put(VERSION);
    output.put(layout.id());
    output.put(zero_based ? 1 : 0);
    output.put(static_cast<Byte>(n));
    output.little_u64(state.length);
    output.little_u64(tuple_count);
    output.bytes(seed.data(), seed.size());
    std::array<Byte, BUFFER_SIZE> buffer{};
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), spool.get()))
        output.bytes(buffer.data(), count);
    if (std::ferror(spool.get())) fail("temporary tuple file read failed");
    output.finish();
}

void decode(std::FILE* source) {
    Reader input(source);
    for (Byte expected : MAGIC)
        if (input.required("header") != expected) fail("not an SPRA binary file");
    const Byte version = input.required("header");
    if (version != 1 && version != VERSION) fail("unsupported format version");
    const Layout layout = Layout::from_id(input.required("header"));
    const Byte flags = input.required("header");
    if (flags & ~1u) fail("unsupported header flags");
    const unsigned n = input.required("header");
    if (n == 0 || n > MAX_N) fail("header N must be 1..32");
    const Count length = input.little_u64("header");
    const Count tuple_count = input.little_u64("header");
    const auto seed_length = static_cast<unsigned>(std::min<Count>(length, n - 1));
    if (tuple_count > length - seed_length) fail("tuple count exceeds decoded length");
    std::array<Byte, MAX_N - 1> seed{};
    for (unsigned i = 0; i < seed_length; ++i) {
        seed[i] = input.required("seed");
        if (seed[i] >= n) fail("seed symbol is outside header alphabet");
    }
    const auto alphabet = flags & 1 ? ZERO_BASED : ONE_BASED;
    Writer output(stdout);
    WordState state(n);
    const auto emit = [&](Byte symbol) {
        state.append(symbol);
        output.put(static_cast<Byte>(alphabet[symbol]));
    };
    for (unsigned i = 0; i < seed_length; ++i) emit(seed[i]);
    TupleReader tuples(input, layout);
    const auto execute = [&](Tuple tuple) {
        const auto [depth, offset] = tuple;
        if (Count(depth) >= length - state.length) fail("tuple exceeds decoded length");
        for (unsigned step = 0; step < depth; ++step) emit(state.next_clean());
        const int distance = static_cast<int>(n) - offset;
        if (distance <= 0) fail("copy reference is not backward");
        emit(state.back(static_cast<unsigned>(distance)));
        if (state.clean()) fail("dirty tuple produces a clean window at symbol " + std::to_string(state.length));
    };
    Tuple previous{};
    bool has_previous = false;
    Count expanded = 0;
    while (expanded < tuple_count) {
        const Tuple token = tuples.get();
        if (version == VERSION && token.offset == 0 && token.depth > 0) {
            if (token.depth == layout.mask()) fail("reserved tuple extension opcode");
            if (!has_previous) fail("repeat marker has no preceding tuple");
            if (token.depth > tuple_count - expanded) fail("repeat marker exceeds logical tuple count");
            for (unsigned i = 0; i < token.depth; ++i) execute(previous);
            expanded += token.depth;
        } else {
            execute(token);
            previous = token;
            has_previous = true;
            ++expanded;
        }
    }
    tuples.finish();
    if (input.get() != EOF) fail("extra bytes after tuple body");
    while (state.length < length) emit(state.next_clean());
    output.finish();
}

struct Options {
    bool encode = false, zero_based = false;
    unsigned n = 0, bits = 4;
    std::string input = "-";
};

Options options(int argc, char** argv) {
    if (argc < 2) fail(USAGE);
    const std::string_view command = argv[1];
    if (command != "encode" && command != "decode") fail(USAGE);
    Options result;
    result.encode = command == "encode";
    bool has_input = false, has_bits = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (result.encode && (arg == "--bits" || arg.starts_with("--bits="))) {
            if (has_bits) fail("duplicate --bits option");
            has_bits = true;
            std::string_view value;
            if (arg == "--bits") {
                if (++i == argc) fail("--bits requires 4 or 6");
                value = argv[i];
            } else value = arg.substr(7);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.bits);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                fail("--bits must be 4 or 6");
            (void)Layout(result.bits);
        } else if (result.encode && (arg == "-z" || arg == "--zero-based")) {
            if (result.zero_based) fail("duplicate zero-based flag");
            result.zero_based = true;
        } else if (result.encode && result.n == 0 && !arg.starts_with('-')) {
            const auto parsed = std::from_chars(arg.data(), arg.data() + arg.size(), result.n);
            if (parsed.ec != std::errc{} || parsed.ptr != arg.data() + arg.size() || result.n == 0 || result.n > MAX_N)
                fail("N must be 1..32");
        } else if (!has_input && (arg == "-" || !arg.starts_with('-'))) {
            result.input = arg;
            has_input = true;
        } else {
            fail("unexpected argument: " + std::string(arg));
        }
    }
    if (result.encode && result.n == 0) fail("encode requires N in 1..32");
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    if ((argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) ||
        (argc == 3 && (std::string_view(argv[1]) == "encode" || std::string_view(argv[1]) == "decode") &&
         (std::string_view(argv[2]) == "--help" || std::string_view(argv[2]) == "-h"))) {
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
    if (args.encode) encode(source, args.n, args.zero_based, Layout(args.bits));
    else decode(source);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "reduced-codec: %s\n", error.what());
    return 1;
}
