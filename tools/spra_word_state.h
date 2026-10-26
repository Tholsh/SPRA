// SPDX-License-Identifier: GPL-3.0-or-later
// Shared bounded symbol history for SPRA decoding and recipe conformance.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace spra {
class WordState {
    using Byte=std::uint8_t;
    using Count=std::uint64_t;
    unsigned n_, suffix_=0;
    std::array<Byte,64> history_{};
    std::array<Count,32> last_{};
    [[noreturn]] static void fail(const std::string &s) { throw std::runtime_error(s); }
public:
    Count length=0;
    explicit WordState(unsigned n):n_(n) { if(!n||n>32) fail("N outside SPRA range 1..32"); }
    Count previous(Byte symbol) const { return last_[symbol]; }
    bool clean() const { return length>=n_&&suffix_==n_; }
    void append(Byte symbol) {
        if(length==std::numeric_limits<Count>::max()) fail("word exceeds uint64 length");
        const Count position=length+1;
        const Count distance=last_[symbol]?position-last_[symbol]:position;
        suffix_=static_cast<unsigned>(std::min<Count>({n_,suffix_+1,distance}));
        history_[length%history_.size()]=symbol; last_[symbol]=position; length=position;
    }
    Byte back(unsigned distance) const {
        if(!distance||distance>length||distance>history_.size())
            fail("copy reference precedes available history at symbol "+std::to_string(length+1));
        return history_[(length-distance)%history_.size()];
    }
    Byte next_clean() const {
        if(clean()) return back(n_);
        if(suffix_<n_-1) fail("clean run follows a repeated suffix at symbol "+std::to_string(length+1));
        std::uint32_t used=0;
        for(unsigned distance=1;distance<n_;++distance) used|=std::uint32_t{1}<<back(distance);
        for(unsigned symbol=0;symbol<n_;++symbol) if(!(used&(std::uint32_t{1}<<symbol))) return static_cast<Byte>(symbol);
        fail("clean window has no missing symbol");
    }
    // Execute one valid logical action. Nearest-copy conformance belongs to
    // the independent strict decoder, not the recipe streaming hot path.
    void execute(unsigned depth,int offset,Count limit) {
        if(length>limit||Count(depth)>=limit-length) fail("tuple exceeds decoded length");
        for(unsigned j=0;j<depth;++j) append(next_clean());
        const int distance=int(n_)-offset;
        if(distance<=0) fail("copy reference is not backward");
        const Byte symbol=back(unsigned(distance));
        append(symbol);
        if(clean()) fail("dirty tuple produces a clean window");
    }
    void finish_clean(Count limit) {
        while(length<limit) append(next_clean());
        if(length!=limit) fail("decoded length mismatch");
    }
};
}
