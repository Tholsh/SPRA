// SPDX-License-Identifier: GPL-3.0-or-later
// Independent wire fixtures; compiled and run by test_reduced_codec.sh.
#define main codec_cli_main
#include "../reduced_codec.cpp"
#undef main
#include <random>
#include <iostream>
#include <tuple>
#ifdef _WIN32
#define DUP _dup
#define DUP2 _dup2
#define CLOSE _close
#define FILENO _fileno
#else
#include <unistd.h>
#define DUP dup
#define DUP2 dup2
#define CLOSE close
#define FILENO fileno
#endif
using Bytes=std::vector<Byte>;
static unsigned checks=0;
static void check(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error("test: "+why);}
static Bytes bytes(std::string_view s) {return Bytes(s.begin(),s.end());}
static Bytes packet(unsigned n,uint64_t length,Bytes seed,std::vector<unsigned> tuples,unsigned bits=4,unsigned flags=0,unsigned version=2,uint64_t logical=UINT64_MAX) {
    Bytes b={'S','P','R','A',Byte(version),Byte(bits*17),Byte(flags),Byte(n)};
    for(auto value:{length,logical==UINT64_MAX?uint64_t(tuples.size()):logical})for(unsigned i=0;i<8;++i)b.push_back(Byte(value>>(8*i)));
    b.insert(b.end(),seed.begin(),seed.end());unsigned accumulator=0,pending=0;
    for(auto t:tuples) {accumulator=(accumulator<<(bits*2))|t;pending+=bits*2;while(pending>=8){pending-=8;b.push_back(Byte(accumulator>>pending));}}
    if(pending)b.push_back(Byte(accumulator<<(8-pending)));
    return b;
}
// Capture the actual FILE-based codec, including binary zero/LF/Ctrl-Z bytes.
static Bytes run(const Bytes& input,unsigned n=0,unsigned bits=4,bool zero=false,std::optional<Count> limit=UINT64_MAX,bool strict=false) {
    File source(std::tmpfile()),target(std::tmpfile());check(bool(source)&&bool(target),"temporary files");
    binary_mode(source.get());binary_mode(target.get());
    check(std::fwrite(input.data(),1,input.size(),source.get())==input.size(),"fixture write");std::rewind(source.get());
    std::fflush(stdout);int saved=DUP(FILENO(stdout));check(saved>=0,"save stdout");
    check(DUP2(FILENO(target.get()),FILENO(stdout))>=0,"capture stdout");
    try {if(n&&bits)encode(source.get(),n,zero,Layout(bits));else if(n)encode(source.get(),n,zero);else decode(source.get(),limit,strict);std::fflush(stdout);}
    catch(...) {std::fflush(stdout);DUP2(saved,FILENO(stdout));CLOSE(saved);throw;}
    DUP2(saved,FILENO(stdout));CLOSE(saved);std::rewind(target.get());Bytes result;
    for(int c=std::fgetc(target.get());c!=EOF;c=std::fgetc(target.get()))result.push_back(Byte(c));
    return result;
}
static void bad(const Bytes& data,std::string_view message="",unsigned n=0,unsigned bits=4) {
    try {run(data,n,bits);}catch(const std::exception& e){check(std::string(e.what()).find(message)!=std::string::npos,"error message: "+std::string(e.what()));return;}
    check(false,"malformed input accepted");
}
static void limited(const Bytes& data,std::optional<Count> limit,std::string_view message) {
    try {run(data,0,4,false,limit);}catch(const std::exception& e){check(std::string(e.what()).find(message)!=std::string::npos,"limit message: "+std::string(e.what()));return;}
    check(false,"length above the limit accepted");
}
static Bytes strict(const Bytes& data) {return run(data,0,4,false,UINT64_MAX,true);}
static void not_canonical(const Bytes& data,const Bytes& word,std::string_view message) {
    check(run(data)==word,"non-canonical file decodes without --strict");
    try {strict(data);}catch(const std::exception& e){check(std::string(e.what()).find(message)!=std::string::npos,"strict message: "+std::string(e.what()));return;}
    check(false,"non-canonical file accepted by --strict");
}
static void vector(const std::string& word,unsigned n,const Bytes& expected,unsigned bits=4,bool zero=false) {
    check(run(bytes(word),n,bits,zero)==expected,"wire vector "+word);check(run(expected)==bytes(word),"fixture decode");
}
int main() try {
    vector("123121321",3,packet(3,9,{0,1},{0x31}));
    vector("012010210",3,packet(3,9,{0,1},{0x31},4,1),4,true);
    check(run(bytes("0 12\r\n010\t210\v\f"),3,4,true)==packet(3,9,{0,1},{0x31},4,1),"whitespace");
    vector("123121321",3,packet(3,9,{0,1},{0xc1},6),6);
    vector("1231213211",3,packet(3,10,{0,1},{0xc1,0xc2},6),6);
    vector("21312",4,packet(4,5,{1,0,2},{2,0},6),6);
    for(unsigned bits:{4u,6u}) {
        unsigned limit=(1u<<bits)-2;
        vector("11111",3,packet(3,5,{0,0},{2,2u<<bits},bits,0,2,3),bits);
        check(run(packet(4,7,{1,0,2},{2,0,2u<<bits},bits,0,2,4))==bytes("2131213"),"repeat zero");
        check(run(packet(3,5,{0,0},{2,2,2},bits,0,1))==bytes("11111"),"legacy repeat");
        check(run(packet(3,9,{0,1},{(3u<<bits)|1},bits,0,1))==bytes("123121321"),"legacy clean");
        for(unsigned repeats:{0u,1u,limit-1,limit,limit+1,2*limit,2*limit+1}) {
            std::vector<unsigned> tuples{2};for(unsigned i=0;i<repeats/limit;++i)tuples.push_back(limit<<bits);if(repeats%limit)tuples.push_back((repeats%limit)<<bits);
            vector(std::string(repeats+3,'1'),3,packet(3,repeats+3,{0,0},tuples,bits,0,2,repeats+1),bits);
        }
        bad(packet(3,4,{0,0},{1u<<bits},bits),"no preceding tuple");
        bad(packet(3,4,{0,0},{((1u<<bits)-1)<<bits},bits),"reserved tuple extension");
        bad(packet(3,8,{0,0},{2,2u<<bits},bits,0,2,2),"exceeds logical tuple count");
        bad(packet(3,4,{0,0},{2,2u<<bits},bits,0,2,3),"exceeds decoded length");
        bad(packet(3,6,{0,0},{2,1u<<bits},bits,0,2,4),"truncated tuple body");
        bad(packet(3,6,{0,1},{(1u<<bits)|2,1u<<bits},bits,0,2,2),"repeated suffix");
        auto valid=packet(3,9,{0,1},{(3u<<bits)|1},bits);
        for(std::size_t size=0;size<valid.size();++size)bad(Bytes(valid.begin(),valid.begin()+size));
        valid.push_back(0);bad(valid,"extra bytes");
        for(unsigned n:{1u,2u,3u,8u,13u,16u,17u,32u})for(bool zero:{false,true}) {
            std::string alphabet=(zero?std::string(ZERO_BASED):std::string(ONE_BASED)).substr(0,n);
            for(auto word:{std::string(),alphabet.substr(0,n>1?n-2:0),alphabet,alphabet+alphabet+alphabet})check(run(run(bytes(word),n,bits,zero))==bytes(word),"alphabet round trip");
        }
        std::string huge="123"+std::string(3000000,'1');check(run(run(bytes(huge),3,bits))==bytes(huge),"buffer boundaries");
    }
    auto six=packet(3,9,{0,1},{0xc1},6);
    for(unsigned padding:{1u,8u,15u}){auto p=six;p.back()|=padding;bad(p,"padding");}
    for(auto [raw,message]:std::vector<std::pair<unsigned,std::string>>{{3,"not backward"},{32,"available history"},{64,"no preceding tuple"}})bad(packet(3,4,{0,1},{raw},6),message);
    auto valid=packet(3,9,{0,1},{0x31});
    for(auto [offset,value]:std::vector<std::pair<unsigned,unsigned>>{{0,'X'},{4,3},{5,0x35},{6,2},{7,0},{7,33},{24,3}}){auto p=valid;p[offset]=Byte(value);bad(p);}
    bad(packet(3,2,{0,1},{1}),"tuple count");bad(packet(3,3,{0,1},{0x11}),"exceeds decoded length");bad(packet(3,3,{0,1},{3}),"not backward");bad(packet(3,3,{0,1},{8}),"available history");bad(packet(3,4,{0,1},{0x10}),"no preceding tuple");bad(packet(3,4,{0,0},{2}),"repeated suffix");
    std::string alphabet(ONE_BASED);
    Bytes seed31;for(unsigned i=0;i<31;++i)seed31.push_back(Byte(i));
    vector(alphabet+std::string(32,alphabet.back())+"1",32,packet(32,65,seed31,{95,31,30u<<6,32},6,0,2,33),6);
    auto positive=run(bytes(alphabet+alphabet.back()),32,6);check(positive[positive.size()-2]==5&&positive.back()==0xf0,"six-bit positive boundary");
    for(unsigned bits:{4u,6u}) {
        unsigned count=bits==4?8:32;std::string depth;for(unsigned i=0;i<count;++i)depth+="12";
        auto packed=run(bytes(depth+"2"),2,bits);check(packed.back()==(bits==4?0xf1:0x10),"depth boundary");bad(bytes(depth+"11"),bits==4?"depth 16":"depth 64",2,bits);
        auto odd=packet(3,7,{0,0},{2,2u<<bits,2u<<bits},bits,0,2,5);check(run(odd)==bytes("1111111"),"chained markers");if(bits==6){odd.back()|=1;bad(odd,"padding");}
    }
    std::string collision="123121321"+std::string(7,'1');auto first=run(bytes(collision+"2"+std::string(7,'1')),3),second=run(bytes(collision+"3"+std::string(7,'1')),3);check(first!=second,"history collision distinguished");check(std::find(first.begin()+26,first.end(),10)!=first.end(),"binary LF tuple");
    auto ctrlz=run(bytes("12121212121212121212121212"),2);check(ctrlz[8]==26&&run(ctrlz)==bytes("12121212121212121212121212"),"binary Ctrl-Z header");
    vector(alphabet.substr(0,16)+"2",16,packet(16,17,Bytes{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14},{0x11}));
    check(run(bytes("123456788"),8).back()==0x17,"positive signed boundary");check(run(bytes("12"+std::string(10,'1')+"2"),3).back()==8,"negative signed boundary");
    vector("21312",4,packet(4,5,{1,0,2},{2,0}));
    vector("12"+std::string(34,'1')+"2",3,packet(3,37,{0,1},{1,2,32u<<6,32},6,0,2,35),6);
    for(auto [word,n,bits,message]:std::vector<std::tuple<std::string,unsigned,unsigned,std::string>>{{"112",3,4,"no earlier occurrence"},{"123x",3,4,"invalid alphabet"},{"124",3,4,"invalid alphabet"},{"1234567899",9,4,"copy distance 1"},{"12"+std::string(11,'1')+"2",3,4,"copy distance 12"},{"12"+std::string(35,'1')+"2",3,6,"copy distance 36"}})bad(bytes(word),message,n,bits);
    check(default_max_symbols(1)==1&&default_max_symbols(3)==18&&default_max_symbols(13)==80951270400ull&&default_max_symbols(19)==2311256907767808000ull&&default_max_symbols(20)==UINT64_MAX,"N * N! limit");
    limited(packet(1,UINT64_MAX,{},{}),std::nullopt,"exceeds N * N! = 1");limited(packet(3,UINT64_MAX,{0,1},{}),std::nullopt,"exceeds N * N! = 18");
    check(run(packet(3,18,{0,1},{}),0,4,false,std::nullopt)==bytes("123123123123123123"),"length N * N! accepted");limited(packet(3,19,{0,1},{}),std::nullopt,"exceeds N * N!");
    check(run(packet(3,5,{0,0},{2,2u<<4},4,0,2,3),0,4,false,5)==bytes("11111"),"length at --max-symbols");limited(packet(3,5,{0,0},{2,2u<<4},4,0,2,3),4,"exceeds --max-symbols 4");
    check(run(packet(2,40,{0},{}),0,4,false,40).size()==40,"--max-symbols above N * N!");limited(packet(32,UINT64_MAX,seed31,{}),1000000,"exceeds --max-symbols");
    std::string clean;for(int i=0;i<40000;++i)clean+="12";vector(clean,2,packet(2,clean.size(),{0},{}));
    // Automatic width: the --bits 4 bytes when every tuple fits, else the --bits 6 bytes.
    for(unsigned repeats:{1u,13u,14u,15u,61u,62u,63u,200u,1000u})check(run(bytes("12"+std::string(repeats,'1')),3,0)==run(bytes("12"+std::string(repeats,'1')),3,4),"automatic four-bit runs");
    for(auto [word,n]:std::vector<std::pair<std::string,unsigned>>{{"12"+std::string(11,'1')+"2",3},{"1212121212121212121212121212121211",2}})check(run(bytes(word),n,0)==run(bytes(word),n,6)&&run(bytes(word),n,0)[5]==0x66,"automatic six-bit fallback");
    bad(bytes("12"+std::string(35,'1')+"2"),"copy distance 36 is outside 6-bit",3,0);bad(bytes("112"),"no earlier occurrence",3,0);
    // Canonical files: what encode writes without --bits; --strict refuses every other valid file.
    check(run(bytes("11111"),3,0)==packet(3,5,{0,0},{2,2u<<4},4,0,2,3)&&strict(packet(3,5,{0,0},{2,2u<<4},4,0,2,3))==bytes("11111"),"canonical 11111");
    not_canonical(packet(3,5,{0,0},{2,1u<<4,1u<<4},4,0,2,3),bytes("11111"),"follows a marker of 1");
    not_canonical(packet(3,5,{0,0},{2,2,2},4,0,2,3),bytes("11111"),"literal repeats");
    not_canonical(packet(3,5,{0,0},{2,2,2},4,0,1),bytes("11111"),"version 0x01");
    check(run(bytes("111"),3,0)==packet(3,3,{0,0},{2}),"nearest reference");
    not_canonical(packet(3,3,{0,0},{1}),bytes("111"),"nearest occurrence is at distance 1");
    not_canonical(packet(3,9,{0,1},{0xc1},6),bytes("123121321"),"every tuple fits four-bit");
    for(unsigned bits:{4u,6u}){unsigned limit=(1u<<bits)-2;
        not_canonical(packet(3,limit+4,{0,0},{2,(limit-1)<<bits,2u<<bits},bits,0,2,limit+2),bytes(std::string(limit+4,'1')),"follows a marker of "+std::to_string(limit-1));}
    std::string wide="12"+std::string(11,'1')+"2";check(strict(run(bytes(wide),3,0))==bytes(wide)&&strict(run(bytes(wide+std::string(100,'1')),3,0))==bytes(wide+std::string(100,'1')),"canonical six-bit");
    for(unsigned repeats:{1u,13u,14u,15u,28u,29u,61u,62u,63u,200u})check(strict(run(bytes("12"+std::string(repeats,'1')),3,0))==bytes("12"+std::string(repeats,'1')),"canonical runs");
    std::mt19937 random(20261017);
    for(unsigned n=2;n<=8;++n)for(unsigned trial=0;trial<5;++trial){std::string word=alphabet.substr(0,n);for(unsigned i=0;i<12;++i)word+=word[word.size()-1-random()%std::min<std::size_t>(word.size(),n+8)];check(run(run(bytes(word),n))==bytes(word),"four-bit generated round trip");check(run(bytes(word),n,0)==run(bytes(word),n),"automatic width keeps four bits");check(strict(run(bytes(word),n,0))==bytes(word),"generated canonical four-bit");}
    for(unsigned n=2;n<=32;++n)for(unsigned trial=0;trial<5;++trial){std::string word=alphabet.substr(0,n);for(unsigned i=0;i<200;++i)word+=word[word.size()-1-random()%std::min<std::size_t>(word.size(),n+32)];check(run(run(bytes(word),n,6))==bytes(word),"generated round trip");check(strict(run(bytes(word),n,0))==bytes(word),"automatic round trip passes --strict");}
    std::cout<<"Codec: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
