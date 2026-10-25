// SPDX-License-Identifier: GPL-3.0-or-later
// Independent oracle for the incremental fixed-rule pair cascade.
#define main bpe_cli_main
#include "../bpe_layer.cpp"
#undef main
#include <iostream>
#include <random>
int main(int argc,char**) try {
    if(argc>1) {
        binary_mode(stdout);
        Count tuples=200000;
        for(unsigned i=1;i<=600;++i)tuples+=1+i%300;
        Writer output(stdout);
        for(Byte byte:std::array<Byte,8>{'S','P','R','A',2,0x44,0,13})output.put(byte);
        for(Count value:{tuples+12,tuples})for(unsigned i=0;i<8;++i)output.put(Byte(value>>(8*i)));
        for(unsigned i=0;i<12;++i)output.put(Byte(i));
        SpraBodyWriter body(output,4);body.put({0,1},200000);
        for(unsigned i=1;i<=600;++i)body.put({0,int(1+i%2)},1+i%300);
        body.finish();output.finish();return 0;
    }
    std::mt19937 random(20261024);
    unsigned checks=0;
    for(unsigned trial=0;trial<300;++trial) {
        const unsigned base=1+random()%16, count=random()%(257-base);
        std::vector<std::pair<Byte,Byte>> rules;
        for(unsigned i=0;i<count;++i)rules.emplace_back(Byte(random()%(base+i)),Byte(random()%(base+i)));
        std::vector<Byte> input(500+random()%2000);for(auto& symbol:input)symbol=Byte(random()%base);
        for(unsigned piece:{1u,2u,31u,65536u}) {
            std::vector<Byte> actual,expected;
            PairStream stream(rules,base,[&](Byte symbol){actual.push_back(symbol);});
            for(std::size_t start=0;start<input.size();start+=piece) {
                const auto end=std::min(input.size(),start+piece);
                std::vector<Byte> offline(input.begin()+start,input.begin()+end);
                for(unsigned i=0;i<rules.size();++i)replace_pair(offline,rules[i],Byte(base+i));
                expected.insert(expected.end(),offline.begin(),offline.end());
                for(std::size_t i=start;i<end;++i)stream.put(input[i]);
                stream.finish();
            }
            if(actual!=expected)throw std::runtime_error("incremental cascade differs from offline pair substitution");
            ++checks;
        }
    }
    // Every possible stage index, including bitmap-word boundaries.
    std::vector<std::pair<Byte,Byte>> rules;
    for(unsigned i=0;i<255;++i)rules.emplace_back(Byte(i),Byte(i));
    std::vector<Byte> expected(100000,0),actual;
    PairStream stream(rules,1,[&](Byte symbol){actual.push_back(symbol);});
    for(auto symbol:expected)stream.put(symbol);
    stream.finish();
    for(unsigned i=0;i<rules.size();++i)replace_pair(expected,rules[i],Byte(i+1));
    if(actual!=expected)throw std::runtime_error("deep cascade mismatch");
    std::cout<<"Streaming pair cascade: "<<checks+1<<" independent checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
