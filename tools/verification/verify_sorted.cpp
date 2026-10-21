// Independent full verifier: decode every window, validate its symbol mask,
// sort exact packed words, and count distinct words. It has no row, rotation,
// course, phase, endpoint, Euler, optimizer, or permutation-ranking code.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv)try{
    need(argc==6,"usage: verify_sorted N ALPHABET WORD EXPECTED_LENGTH FRESH_RECEIPT");
    unsigned n=std::stoul(argv[1]);std::string alphabet=argv[2];
    need(n>=1&&n<=15&&alphabet.size()==n,"alphabet degree");auto a=alphabet;std::sort(a.begin(),a.end());need(std::adjacent_find(a.begin(),a.end())==a.end(),"alphabet repeats symbol");
    std::ifstream in(argv[3],std::ios::binary);need(bool(in),"word read");std::string w((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());need(!in.bad(),"word IO error");if(!w.empty()&&w.back()=='\n')w.pop_back();need(w.size()==std::stoull(argv[4]),"word length");
    std::vector<uint8_t> symbols;symbols.reserve(w.size());for(char c:w){auto i=alphabet.find(c);need(i<n,"word outside alphabet");symbols.push_back(uint8_t(i));}
    std::vector<uint64_t> windows;windows.reserve(w.size());
    for(size_t j=0;j+n<=symbols.size();++j){unsigned mask=0;uint64_t code=0;for(unsigned k=0;k<n;++k){mask|=1u<<symbols[j+k];code=(code<<4)|symbols[j+k];}if(mask==((1u<<n)-1))windows.push_back(code);}
    size_t occurrences=windows.size();std::sort(windows.begin(),windows.end());windows.erase(std::unique(windows.begin(),windows.end()),windows.end());uint64_t required=1;for(unsigned j=2;j<=n;++j)required*=j;
    bool pass=windows.size()==required;
    std::filesystem::path out=argv[5];need(!std::filesystem::exists(out),"receipt overwrite");std::ofstream receipt(out);need(bool(receipt),"receipt write");
    receipt<<"{\"checker\":\"independent-sorted-exact-nibble-windows\",\"n\":"<<n<<",\"length\":"<<w.size()<<",\"distinct\":"<<windows.size()<<",\"required\":"<<required<<",\"missing\":"<<required-windows.size()<<",\"occurrences\":"<<occurrences<<",\"repeat_occurrences\":"<<occurrences-windows.size()<<",\"pass\":"<<(pass?"true":"false")<<"}\n";
    need(bool(receipt),"receipt IO error");std::cout<<(pass?"PASS":"FAIL")<<" length="<<w.size()<<" distinct="<<windows.size()<<"/"<<required<<" repeats="<<occurrences-windows.size()<<'\n';return pass?0:3;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
