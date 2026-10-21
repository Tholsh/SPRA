// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Theo H.
// Distributed without warranty; see the repository LICENSE for full terms.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using Symbol = uint8_t;
constexpr std::size_t MAX_N = 13;
constexpr const char* RED = "\033[31m";
constexpr const char* RESET = "\033[0m";
constexpr const char* USAGE = "usage: paint-waste [-s] [-m] [-z|--zero-based] N < WORD\n";
static_assert(sizeof(std::size_t) >= 8, "Paint waste requires a 64-bit platform.");

bool is_dirty(std::span<const Symbol> window) {
    unsigned seen = 0;
    for (Symbol symbol : window) {
        const unsigned bit = 1u << symbol;
        if (seen & bit) return true;
        seen |= bit;
    }
    return false;
}

uint8_t which_index(std::span<const Symbol> bigwindow, std::size_t n) {
    for (std::size_t index = bigwindow.size() - 2; index > 0; --index) {
        if (bigwindow[index] == bigwindow.back()) {
            return static_cast<uint8_t>(2 * n - index);
        }
    }
    return 0;
}

// Lehmer rank of a clean window. Symbols are normalized to 0..N-1, so ranking
// and coverage are independent of the user's choice of alphabet labels.
std::size_t permutation_rank(std::span<const Symbol> window,
                            const std::array<std::size_t, MAX_N + 1>& factorial) {
    unsigned unused = (1u << window.size()) - 1;
    std::size_t rank = 0;
    for (std::size_t i = 0; i < window.size(); ++i) {
        const unsigned bit = 1u << window[i];
        rank += std::popcount(unused & (bit - 1)) * factorial[window.size() - i - 1];
        unused ^= bit;
    }
    return rank;
}

bool confirm_short_output(std::size_t n) {
    // stdin belongs to the word, which may be piped or redirected. Confirmation
    // must come from the controlling terminal, never from the corpus stream.
#ifdef _WIN32
    HANDLE terminal = CreateFileA("CONIN$", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD mode = 0;
    const bool has_terminal = terminal != INVALID_HANDLE_VALUE && GetConsoleMode(terminal, &mode);
#else
    std::ifstream terminal("/dev/tty");
    const bool has_terminal = bool(terminal);
#endif
    if (!has_terminal) {
#ifdef _WIN32
        if (terminal != INVALID_HANDLE_VALUE) CloseHandle(terminal);
#endif
        std::cerr << "N=" << n << " requires statistics-only output. "
                     "No terminal available to ask; rerun with -s to accept.\n";
        return false;
    }
    std::cerr << "N=" << n << " requires statistics-only (-s) output. "
                 "Continue? [y/N]: " << std::flush;
    std::string answer;
#ifdef _WIN32
    // Use the console API directly: file-stream buffering can wait for more
    // input even though the user has already entered a complete console line.
    std::array<char, 256> reply{};
    DWORD count = 0;
    const bool read = ReadConsoleA(terminal, reply.data(), reply.size() - 1, &count, nullptr);
    CloseHandle(terminal);
    if (!read) return false;
    answer.assign(reply.data(), count);
#else
    if (!std::getline(terminal, answer)) return false;
#endif
    const auto first = answer.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    answer = answer.substr(first, answer.find_last_not_of(" \t\r\n") - first + 1);
    for (char& c : answer) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return answer == "y" || answer == "yes";
}

int main(int argc, char** argv) try {
    bool short_output = false, bracket_output = false, zero_based = false;
    std::string_view degree;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::cout << USAGE
                      << "  -s  statistics only (required above N=9)\n"
                         "  -m  mark each dirty character as [X] instead of using ANSI color\n"
                         "  -z, --zero-based  use 0123456789ABC instead of 123456789ABCD\n"
                         "  N must be between 1 and 13; uppercase letters are symbols 10 onward.\n";
            return 0;
        }
        if (argument == "-s" && !short_output) {
            short_output = true;
        } else if (argument == "-m" && !bracket_output) {
            bracket_output = true;
        } else if ((argument == "-z" || argument == "--zero-based") && !zero_based) {
            zero_based = true;
        } else if (degree.empty() && !argument.starts_with('-')) {
            degree = argument;
        } else {
            std::cerr << USAGE;
            return 1;
        }
    }
    std::size_t n = 0;
    if (degree.empty()) {
        std::cerr << USAGE;
        return 1;
    }
    const auto parsed = std::from_chars(degree.data(), degree.data() + degree.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != degree.data() + degree.size() || n == 0 || n > MAX_N) {
        std::cerr << "bad N: " << degree << '\n';
        return 1;
    }
    if (n > 9 && !short_output) {
        if (!confirm_short_output(n)) {
            std::cerr << "analysis cancelled\n";
            return 1;
        }
        short_output = true;
    }
    const std::string_view alphabet = zero_based ? "0123456789ABC" : "123456789ABCD";
    std::array<std::size_t, MAX_N + 1> factorial{};
    factorial[0] = 1;
    for (std::size_t i = 1; i <= n; ++i) factorial[i] = factorial[i - 1] * i;
    std::vector<bool> covered(factorial[n], false);

    // Retain the whole word only when it must be painted. Statistics-only mode
    // keeps a rolling 2N-symbol history plus one coverage bit per permutation.
    std::vector<Symbol> symbols;
    std::vector<bool> dirty_at;
    std::vector<uint8_t> waste_indexes;
    std::vector<std::size_t> waste_depths;
    std::array<Symbol, 2 * MAX_N> history{};
    std::size_t history_size = 0, length = 0, dirty_count = 0, distinct = 0;
    std::size_t dirty_run = 0, clean_run = 0, max_dirty_run = 0, max_clean_run = 0;
    std::size_t dirty_runs = 0, clean_runs = 0;

    // Buffer stream reads: large corpus words should not pay an istream call
    // for every individual symbol.
    std::array<char, 65536> input{};
    while (std::cin.read(input.data(), input.size()) || std::cin.gcount()) {
        const auto count = std::cin.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            const char c = input[static_cast<std::size_t>(i)];
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            const auto label = alphabet.find(c);
            if (label == std::string_view::npos) {
                std::cerr << "bad character: " << c << '\n';
                return 1;
            }
            if (label >= n) {
                std::cerr << "bad symbol: " << c << '\n';
                return 1;
            }
            const Symbol symbol = static_cast<Symbol>(label);
            ++length;
            if (!short_output) {
                symbols.push_back(symbol);
                dirty_at.push_back(false);
            }
            if (history_size == 2 * n) {
                std::move(history.begin() + 1, history.begin() + history_size, history.begin());
                --history_size;
            }
            history[history_size++] = symbol;
            if (length < n) continue;
            const std::span<const Symbol> window{history.data() + history_size - n, n};
            if (is_dirty(window)) {
                ++dirty_count;
                if (dirty_run == 0) ++dirty_runs;
                ++dirty_run;
                max_dirty_run = std::max(max_dirty_run, dirty_run);
                if (!short_output) {
                    dirty_at.back() = true;
                    waste_indexes.push_back(which_index({history.data(), history_size}, n));
                    waste_depths.push_back(clean_run);
                }
                clean_run = 0;
            } else {
                dirty_run = 0;
                if (clean_run == 0) ++clean_runs;
                ++clean_run;
                max_clean_run = std::max(max_clean_run, clean_run);
                const auto rank = permutation_rank(window, factorial);
                if (!covered[rank]) {
                    covered[rank] = true;
                    ++distinct;
                }
            }
        }
    }
    if (std::cin.bad() || (std::cin.fail() && !std::cin.eof())) {
        std::cerr << "input read failed\n";
        return 1;
    }

    if (!short_output) {
        bool red_on = false;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (bracket_output) {
                if (dirty_at[i]) std::cout << '[';
                std::cout << alphabet[symbols[i]];
                if (dirty_at[i]) std::cout << ']';
            } else {
                if (dirty_at[i] != red_on) {
                    std::cout << (dirty_at[i] ? RED : RESET);
                    red_on = dirty_at[i];
                }
                std::cout << alphabet[symbols[i]];
            }
        }
        if (red_on) std::cout << RESET;
        std::cout << "\n\nreduced_alphabet_words:\n" << std::hex;
        for (std::size_t i = 0; i < waste_indexes.size(); ++i) {
            std::cout << '[' << waste_depths[i] << ':' << static_cast<unsigned>(waste_indexes[i]) << "] ";
        }
        std::cout << "\n\nstatistics:\n";
    }
    const std::size_t windows = length >= n ? length - n + 1 : 0;
    const std::size_t clean = windows - dirty_count;
    // Painting style never changes the statistics or their text format.
    const std::array<std::pair<std::string_view, std::size_t>, 15> statistics{{
        {"length", length},
        {"symbols", length},
        {"n", n},
        {"windows", windows},
        {"dirty", dirty_count},
        {"clean", clean},
        {"max_dirty_run", max_dirty_run},
        {"max_clean_run", max_clean_run},
        {"dirty_runs", dirty_runs},
        {"clean_runs", clean_runs},
        {"required_permutations", factorial[n]},
        {"distinct_permutations", distinct},
        {"missing_permutations", factorial[n] - distinct},
        {"repeated_permutation_windows", clean - distinct},
        {"valid_superpermutation", distinct == factorial[n] ? 1u : 0u},
    }};
    std::cout << std::dec;
    for (const auto& [key, value] : statistics) {
        std::cout << key << ": " << value << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "analysis failed: " << error.what() << '\n';
    return 1;
}
