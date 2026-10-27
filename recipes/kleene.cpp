// SPDX-License-Identifier: GPL-3.0-or-later
// A finite tuple language: atoms, concatenation, and counted repetition.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "../tools/spra_word_state.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using U = uint64_t;
using Id = uint32_t;
[[noreturn]] void fail(const std::string& s) { throw std::runtime_error(s); }
U add(U a, U b) { if (b > UINT64_MAX-a) fail("count overflow"); return a+b; }
U mul(U a, U b) { if (b && a > UINT64_MAX/b) fail("count overflow"); return a*b; }
struct Limits {
    size_t max_nodes=2000000, max_edges=32000000;
    size_t max_source_bytes=256u*1024u*1024u, max_nesting=256;
};
struct Input {
    std::array<unsigned char,65536> b{}; size_t pos=0, end=0;
    int get() {
        if(pos==end) { end=fread(b.data(),1,b.size(),stdin); pos=0;
            if(!end) { if(ferror(stdin)) fail("input read"); return EOF; } }
        return b[pos++];
    }
    unsigned byte() { int c=get(); if(c==EOF) fail("truncated SPRA"); return unsigned(c); }
};
struct Output {
    std::array<unsigned char,65536> b{}; size_t size=0; U bytes=0;
    void flush() { if(size && fwrite(b.data(),1,size,stdout)!=size) fail("output write"); size=0; }
    void put(unsigned c) { if(size==b.size()) flush(); b[size++]=static_cast<unsigned char>(c); ++bytes; }
    void finish() { flush(); if(fflush(stdout)) fail("output flush"); }
};
struct Header {
    std::vector<unsigned char> data;
    unsigned bits=0, n=0; U length=0, tuples=0;
    void check() {
        if(data.size()<24 || std::string(data.begin(),data.begin()+4)!="SPRA" || data[4]!=2 ||
           (data[5]!=0x44 && data[5]!=0x66) || data[6]>1 || !data[7] || data[7]>32) fail("invalid SPRA 0.2 header");
        bits=data[5]&15; n=data[7]; length=tuples=0;
        for(unsigned j=0;j<8;++j) { length|=U(data[8+j])<<(8*j); tuples|=U(data[16+j])<<(8*j); }
        U seed=std::min<U>(length,n-1);
        if(data.size()!=24+seed || tuples>length-seed) fail("invalid seed or tuple count");
        for(size_t j=24;j<data.size();++j) if(data[j]>=n) fail("invalid seed label");
    }
};
struct Node {
    char kind='t'; unsigned depth=0; int offset=0; U count=1, length=1;
    std::vector<Id> children;
};
U mix(U x) { x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; return x^(x>>31); }
struct Grammar {
    Limits limits;
    std::vector<Node> nodes;
    std::unordered_map<U,std::vector<Id>> index;
    size_t edges=0;
    explicit Grammar(const Limits &options={}):limits(options) {}
    Id intern(Node node) {
        U hash=mix(unsigned(node.kind)^mix(node.depth)^mix(U(node.offset+64))^mix(node.count));
        for(Id c:node.children) hash=mix(hash^mix(c+1));
        auto &bucket=index[hash];
        for(Id id:bucket) { auto &a=nodes[id];
            if(a.kind==node.kind && a.depth==node.depth && a.offset==node.offset && a.count==node.count && a.children==node.children) return id; }
        if(nodes.size()>=limits.max_nodes) fail("--max-nodes limit exceeded ("+std::to_string(limits.max_nodes)+"); increase --max-nodes");
        if(node.children.size()>limits.max_edges-edges) fail("--max-edges limit exceeded ("+std::to_string(limits.max_edges)+"); increase --max-edges");
        Id id=static_cast<Id>(nodes.size()); edges+=node.children.size(); nodes.push_back(std::move(node)); bucket.push_back(id); return id;
    }
    Id atom(unsigned d,int o) { Node x; x.depth=d; x.offset=o; return intern(std::move(x)); }
    Id repeat(Id id,U count) {
        if(count==1) return id;
        if(nodes[id].kind=='r') { count=mul(count,nodes[id].count); id=nodes[id].children[0]; }
        Node x; x.kind='r'; x.count=count; x.children={id}; x.length=mul(nodes[id].length,count); return intern(std::move(x));
    }
    Id concat(const std::vector<Id>& ids) {
        if(ids.size()==1) return ids[0];
        std::vector<Id> compact;
        for(size_t i=0;i<ids.size();) { size_t j=i+1; while(j<ids.size() && ids[i]==ids[j]) ++j;
            compact.push_back(repeat(ids[i],j-i)); i=j; }
        if(compact.size()==1) return compact[0];
        Node x; x.kind='c'; x.children=std::move(compact); x.length=0;
        for(Id id:x.children) x.length=add(x.length,nodes[id].length);
        return intern(std::move(x));
    }
};
// Content boundaries depend on tuple/rule identities, allowing shifted repeated
// passages to resynchronize. Every level holds at most 256 pending references.
struct Builder {
    Grammar &g;
    std::array<std::vector<Id>,64> pending;
    std::array<U,64> hash{};
    explicit Builder(Grammar &grammar):g(grammar){}
    void push(Id id,size_t level=0) {
        if(level>=pending.size()) fail("grammar depth exceeded");
        auto &v=pending[level]; v.push_back(id); hash[level]=(hash[level]<<1)+mix(id+1);
        if(v.size()==256 || (v.size()>=16 && (hash[level]&63)==0)) {
            Id block=g.concat(v); v.clear(); hash[level]=0; push(block,level+1);
        }
    }
    Id finish() {
        for(size_t level=0;level<pending.size();++level) {
            if(pending[level].empty()) continue;
            bool higher=false;
            for(size_t j=level+1;j<pending.size();++j) higher|=!pending[j].empty();
            Id block=g.concat(pending[level]); pending[level].clear();
            if(!higher) return block;
            push(block,level+1);
        }
        fail("empty tuple stream");
    }
};
// Mine repeated sequences of *logical tuple runs*, not packed bytes or RLE
// markers. LZ matches become closed expressions; emitted recipes contain no
// backreferences to an external stream. Fixed-size mining windows bound input
// workspace, while hash-consed expressions are shared across all windows.
struct Phrases {
    Grammar &g; Builder &builder; std::vector<Id> input;
    static constexpr size_t WINDOW=262144;
    std::unordered_map<U,Id> ranges;
    explicit Phrases(Grammar &grammar,Builder &b):g(grammar),builder(b) { input.reserve(WINDOW); }
    Id range(size_t begin,size_t length) {
        if(length==1) return input[begin];
        U key=(U(begin)<<32)|length;
        auto found=ranges.find(key); if(found!=ranges.end()) return found->second;
        size_t half=length/2;
        Id left=range(begin,half),right=range(begin+half,length-half);
        Id result=g.concat({left,right}); ranges.emplace(key,result); return result;
    }
    void flush() {
        if(input.empty()) return;
        ranges.clear();
        std::vector<int> table(1u<<20,-1);
        auto hash=[&](size_t pos) { U h=0; for(size_t j=0;j<4;++j) h=mix(h^mix(input[pos+j]+1)); return size_t(h&(table.size()-1)); };
        size_t p=0,literals=0;
        while(p+4<=input.size()) {
            size_t slot=hash(p); int candidate=table[slot]; table[slot]=int(p);
            size_t length=0;
            if(candidate>=0) while(p+length<input.size() && input[size_t(candidate)+length]==input[p+length]) ++length;
            if(length<4) { ++p; continue; }
            if(literals<p) builder.push(range(literals,p-literals));
            size_t period=p-size_t(candidate),base=std::min(length,period);
            Id expr=range(size_t(candidate),base);
            if(length>period) {
                expr=g.repeat(expr,length/period);
                if(length%period) expr=g.concat({expr,range(size_t(candidate),length%period)});
            }
            builder.push(expr);
            size_t end=p+length;
            for(size_t q=p+1;q<end && q+4<=input.size();++q) table[hash(q)]=int(q);
            p=end; literals=p;
        }
        if(literals<input.size()) builder.push(range(literals,input.size()-literals));
        input.clear();
    }
    void push(Id atom) { input.push_back(atom); if(input.size()==WINDOW) flush(); }
    void finish() { flush(); }
};

void write_kl(const Header &h,const Grammar &g,Id root);
void compile(const Limits &limits) {
    Input input; Header h;
    for(unsigned i=0;i<24;++i) h.data.push_back(static_cast<unsigned char>(input.byte()));
    // Validate fixed fields before trusting N or allocating seed storage.
    if(h.data[7]<1 || h.data[7]>32) fail("invalid N");
    U length=0; for(unsigned i=0;i<8;++i) length|=U(h.data[8+i])<<(8*i);
    for(U i=0;i<std::min<U>(length,h.data[7]-1);++i) h.data.push_back(static_cast<unsigned char>(input.byte()));
    h.check(); Grammar grammar(limits); Builder builder(grammar); Phrases phrases(grammar,builder);
    if(!h.tuples) { if(input.get()!=EOF) fail("extra SPRA bytes"); Node empty; empty.kind='c'; empty.length=0; Id root=grammar.intern(empty); write_kl(h,grammar,root); return; }
    unsigned pending=0,bits=0,mask=(1u<<h.bits)-1; U expanded=0,run=0; unsigned prev=0; bool have=false;
    auto flush=[&] { if(run) phrases.push(grammar.repeat(grammar.atom(prev>>h.bits, int(prev&mask)<int(1u<<(h.bits-1))?int(prev&mask):int(prev&mask)-int(1u<<h.bits)),run)); run=0; };
    while(expanded<h.tuples) {
        while(bits<2*h.bits) { pending=(pending<<8)|input.byte(); bits+=8; }
        bits-=2*h.bits; unsigned token=pending>>bits; pending&=(1u<<bits)-1;
        unsigned depth=token>>h.bits,low=token&mask; U count=1;
        if(depth && !low) { if(!have || depth==mask || depth>h.tuples-expanded) fail("invalid RLE marker"); count=depth; }
        else { if(!have || token!=prev) { flush(); prev=token; have=true; } }
        run=add(run,count); expanded+=count;
    }
    flush(); if(pending || input.get()!=EOF) fail("padding or extra SPRA bytes");
    phrases.finish(); Id root=builder.finish(); if(grammar.nodes[root].length!=h.tuples) fail("internal tuple count");
    write_kl(h,grammar,root);
}
#include "kleene_source.h"

struct Wire {
    Output out; unsigned bits,mask,pending=0,count=0; bool have=false; unsigned depth=0; int offset=0; U run=0,tokens=0;
    explicit Wire(unsigned width):bits(width),mask((1u<<width)-1){}
    void token(unsigned d,int o) {
        unsigned value=(d<<bits)|(unsigned(o)&mask); pending=(pending<<(2*bits))|value; count+=2*bits;
        while(count>=8) { count-=8; out.put(pending>>count); }
        pending&=(1u<<count)-1; ++tokens;
    }
    void flush_run() {
        if(!run) return;
        token(depth,offset); U left=run-1;
        while(left) { unsigned take=unsigned(std::min<U>(left,mask-1)); token(take,0); left-=take; }
        run=0;
    }
    void put(unsigned d,int o,U repetitions) {
        if(have && d==depth && o==offset) run=add(run,repetitions);
        else { flush_run(); have=true; depth=d; offset=o; run=repetitions; }
    }
    void finish() { flush_run(); if(count) out.put(pending<<(8-count)); out.finish(); }
};
void emit(const Header &h,const Grammar &g,Id root) {
    spra::WordState state(h.n);
    for(size_t i=24;i<h.data.size();++i) state.append(h.data[i]);
    Wire wire(h.bits); for(unsigned c:h.data) wire.out.put(c);
    struct Frame { Id id; U next=0; };
    std::vector<Frame> stack{{root,0}};
    U emitted=0;
    while(!stack.empty()) {
        auto &f=stack.back(); auto &node=g.nodes[f.id];
        if(node.kind=='t') { state.execute(node.depth,node.offset,h.length); wire.put(node.depth,node.offset,1); ++emitted; stack.pop_back(); }
        else if(node.kind=='r' && g.nodes[node.children[0]].kind=='t') {
            auto &t=g.nodes[node.children[0]];
            for(U j=0;j<node.count;++j) state.execute(t.depth,t.offset,h.length);
            wire.put(t.depth,t.offset,node.count); emitted=add(emitted,node.count); stack.pop_back();
        } else if(node.kind=='r') { if(f.next==node.count) stack.pop_back(); else { ++f.next; stack.push_back({node.children[0],0}); } }
        else { if(f.next==node.children.size()) stack.pop_back(); else { Id child=node.children[size_t(f.next++)]; stack.push_back({child,0}); } }
    }
    if(emitted!=h.tuples) fail("emitted count mismatch");
    state.finish_clean(h.length);
    wire.finish();
    std::cerr<<"{\"logical_tuples\":"<<emitted<<",\"physical_tokens\":"<<wire.tokens<<",\"spra_bytes\":"<<wire.out.bytes<<"}\n";
}
// Evaluate lengths and canonical RLE cost algebraically, without expanding
// either the tuple sequence or the raw word. Boundaries can merge equal runs.
struct Summary {
    U tuples=0, clean=0, runs=0, tokens=0, prefix=0, suffix=0;
    unsigned first=0,last=0;
};
U run_tokens(U count,unsigned marker) { return count?1+(count-1)/marker+((count-1)%marker!=0):0; }
Summary combine(Summary a,Summary b,unsigned marker) {
    if(!a.tuples) return b;
    if(!b.tuples) return a;
    Summary x{add(a.tuples,b.tuples),add(a.clean,b.clean),add(a.runs,b.runs),add(a.tokens,b.tokens),a.prefix,b.suffix,a.first,b.last};
    if(a.last==b.first) {
        --x.runs;
        x.tokens-=run_tokens(a.suffix,marker)+run_tokens(b.prefix,marker);
        x.tokens=add(x.tokens,run_tokens(add(a.suffix,b.prefix),marker));
        if(a.prefix==a.tuples) x.prefix=add(a.tuples,b.prefix);
        if(b.suffix==b.tuples) x.suffix=add(b.tuples,a.suffix);
    }
    return x;
}
void audit(const Header &h,const Grammar &g,Id root) {
    unsigned marker=(1u<<h.bits)-2; std::vector<Summary> summaries; summaries.reserve(g.nodes.size());
    for(auto &node:g.nodes) {
        Summary x;
        if(node.kind=='t') { unsigned key=(node.depth<<6)|unsigned(node.offset+32); x={1,node.depth,1,1,1,1,key,key}; }
        else if(node.kind=='c') { for(Id child:node.children) x=combine(x,summaries[child],marker); }
        else { U count=node.count; Summary power=summaries[node.children[0]];
            while(count) { if(count&1) x=combine(x,power,marker); count>>=1; if(count) power=combine(power,power,marker); } }
        summaries.push_back(x);
    }
    auto x=summaries[root]; U covered=add(h.data.size()-24,add(x.tuples,x.clean));
    if(covered>h.length) fail("tuple depths exceed declared word length");
    U wire_bits=mul(x.tokens,2*h.bits),bytes=add(h.data.size(),wire_bits/8+(wire_bits%8!=0));
    std::cout<<"{\"n\":"<<h.n<<",\"bits\":"<<h.bits<<",\"symbols\":"<<h.length<<",\"logical_tuples\":"<<x.tuples
             <<",\"tuple_clean_steps\":"<<x.clean<<",\"trailing_clean_steps\":"<<h.length-covered<<",\"runs\":"<<x.runs
             <<",\"physical_tokens\":"<<x.tokens<<",\"spra_bytes\":"<<bytes<<",\"rules\":"<<g.nodes.size()<<",\"edges\":"<<g.edges<<"}\n";
}
void usage(std::ostream &out) {
    out<<"usage: kleene [LIMITS] compile < input.spra > output.kl\n"
          "       kleene [LIMITS] emit|info|audit FILE.kl|-\n"
          "LIMITS (positive decimal counts; may appear before or after the command):\n"
          "  --max-nodes COUNT         expression nodes and named definitions (default 2000000)\n"
          "  --max-edges COUNT         expression child references (default 32000000)\n"
          "  --max-source-bytes COUNT  decompressed .kl bytes (default 268435456)\n"
          "  --max-nesting COUNT       expression/comment nesting (default 256)\n"
          "These limits do not truncate output or change recipe semantics.\n";
}
struct Arguments {
    Limits limits;
    std::vector<std::string> positional;
    bool help=false;
};
Arguments parse_arguments(int argc,char **argv) {
    Arguments result;
    auto &limits=result.limits;
    auto &positional=result.positional;
    bool options=true;
    for(int i=1;i<argc;++i) {
        std::string arg=argv[i];
        if(options && arg=="--") { options=false; continue; }
        if(options && arg=="--help") { result.help=true; continue; }
        if(options && arg.starts_with("--")) {
            size_t equal=arg.find('=');
            std::string name=arg.substr(0,equal), value;
            size_t *target=nullptr;
            if(name=="--max-nodes") target=&limits.max_nodes;
            else if(name=="--max-edges") target=&limits.max_edges;
            else if(name=="--max-source-bytes") target=&limits.max_source_bytes;
            else if(name=="--max-nesting") target=&limits.max_nesting;
            else fail("unknown option "+name);
            if(equal!=std::string::npos) value=arg.substr(equal+1);
            else if(i+1<argc) value=argv[++i];
            else fail("missing count for "+name);
            U count=0; auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),count);
            if(value.empty()||ec!=std::errc{}||end!=value.data()+value.size()||!count||count>std::numeric_limits<size_t>::max())
                fail(name+" requires a positive decimal count that fits size_t");
            if(name=="--max-nodes" && count>std::numeric_limits<Id>::max()) fail("--max-nodes exceeds the 32-bit node ID range");
            *target=size_t(count);
        } else positional.push_back(std::move(arg));
    }
    return result;
}
int main(int argc,char **argv) try {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    auto args=parse_arguments(argc,argv);
    const auto &limits=args.limits;
    const auto &positional=args.positional;
    if(args.help) { usage(std::cout); return 0; }
    if(positional.size()==1 && positional[0]=="compile") { compile(limits); return 0; }
    if(positional.size()==2 && (positional[0]=="emit" || positional[0]=="info" || positional[0]=="audit")) {
        const std::string &command=positional[0], &path=positional[1];
        std::ifstream file;
        if(path!="-") { file.open(path); if(!file) fail("cannot open recipe"); }
        std::istream &source=path=="-"?std::cin:file; Id root=0;
        auto [h,g]=KlSource(source,limits).read(root);
        if(command=="emit") emit(h,g,root);
        else if(command=="audit") audit(h,g,root);
        else std::cout<<"{\"n\":"<<h.n<<",\"bits\":"<<h.bits<<",\"symbols\":"<<h.length<<",\"logical_tuples\":"<<h.tuples<<",\"rules\":"<<g.nodes.size()<<",\"edges\":"<<g.edges<<",\"root\":"<<root<<"}\n";
        return 0;
    }
    usage(std::cerr); return 1;
} catch(const std::exception &e) { std::cerr<<"kleene: "<<e.what()<<'\n'; return 1; }
