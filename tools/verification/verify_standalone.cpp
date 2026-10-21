// Independent literal scanner. No constructor or upstream verifier imports.
// Rolling symbol frequencies identify permutations; inversion counts give rank.
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char** argv)try{
    if(argc!=5)throw std::runtime_error("usage: verify_standalone N ALPHABET INPUT EXPECTED_LENGTH");int n=std::stoi(argv[1]);std::string alphabet=argv[2];uint64_t expected=std::stoull(argv[4]);if(n<2||n>12||alphabet.size()!=size_t(n))throw std::runtime_error("alphabet/degree");std::array<int,256> decode;decode.fill(-1);for(int j=0;j<n;++j){auto c=uint8_t(alphabet[j]);if(decode[c]>=0)throw std::runtime_error("repeated alphabet symbol");decode[c]=j;}std::ifstream in(argv[3],std::ios::binary);std::string raw((std::istreambuf_iterator<char>(in)),{});if(!in)throw std::runtime_error("input read");if(!raw.empty()&&raw.back()=='\n')raw.pop_back();if(raw.size()!=expected)throw std::runtime_error("unexpected length");std::vector<uint8_t> word;word.reserve(raw.size());for(uint8_t c:raw){if(decode[c]<0)throw std::runtime_error("symbol");word.push_back(uint8_t(decode[c]));}uint64_t total=1;for(int j=2;j<=n;++j)total*=j;std::vector<uint8_t> seen(total);std::array<int,14> frequency{};uint64_t unique=0,occurrences=0;for(size_t j=0;j<word.size();++j){++frequency[word[j]];if(j>=size_t(n))--frequency[word[j-n]];if(j+1<size_t(n))continue;bool clean=true;for(int a=0;a<n;++a)clean&=frequency[a]==1;if(!clean)continue;uint64_t rank=0;size_t start=j+1-n;for(int a=0;a<n;++a){int smaller=0;for(int b=a+1;b<n;++b)smaller+=word[start+b]<word[start+a];rank=rank*(n-a)+smaller;}if(rank>=total)throw std::runtime_error("rank overflow");if(!seen[rank]){seen[rank]=1;++unique;}++occurrences;}std::cout<<"{\"n\":"<<n<<",\"length\":"<<word.size()<<",\"distinct\":"<<unique<<",\"required\":"<<total<<",\"missing\":"<<total-unique<<",\"occurrences\":"<<occurrences<<"}\n";return unique==total?0:3;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
