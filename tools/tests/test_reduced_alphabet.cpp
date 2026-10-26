// SPDX-License-Identifier: GPL-3.0-or-later
// Independent window-set oracle; see docs/tools.md.
#define main paint_cli_main
#include "../paint_waste.cpp"
#undef main
#include <sstream>
#include <map>
#include <set>
#include <random>
static unsigned checks=0;
static void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
struct Result {int status;std::string out,err;};
static Result run(std::string word,std::vector<std::string> args) {
    args.insert(args.begin(),"paint-waste");std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());
    std::istringstream input(word);std::ostringstream output,error;
    auto in=std::cin.rdbuf(input.rdbuf());auto out=std::cout.rdbuf(output.rdbuf());auto err=std::cerr.rdbuf(error.rdbuf());
    std::cin.clear();std::cout.clear();std::cerr.clear();auto flags=std::cout.flags();
    int status=paint_cli_main(int(argv.size()),argv.data());
    std::cin.rdbuf(in);std::cout.rdbuf(out);std::cerr.rdbuf(err);std::cin.clear();std::cout.flags(flags);
    return {status,output.str(),error.str()};
}
static std::map<std::string,uint64_t> stats(const std::string& word,unsigned n,bool zero=false) {
    std::vector<std::string> args={"-s",std::to_string(n)};if(zero)args.push_back("-z");auto r=run(word,args);check(r.status==0&&r.err.empty(),"stats command");
    std::map<std::string,uint64_t> values;std::istringstream lines(r.out);std::string line;
    while(std::getline(lines,line)){auto colon=line.find(": ");check(colon!=std::string::npos,"stat line");values[line.substr(0,colon)]=std::stoull(line.substr(colon+2));}return values;
}
static void oracle(const std::string& word,unsigned n,bool zero=false) {
    auto s=stats(word,n,zero);uint64_t clean=0,windows=word.size()<n?0:word.size()-n+1,factorial=1;std::set<std::string> distinct;
    for(unsigned i=1;i<=n;++i)factorial*=i;
    for(std::size_t i=0;i<windows;++i){auto window=word.substr(i,n);if(std::set<char>(window.begin(),window.end()).size()==n){++clean;distinct.insert(window);}}
    check(s["length"]==word.size()&&s["windows"]==windows&&s["clean"]==clean&&s["dirty"]==windows-clean,"window counts");
    check(s["distinct_permutations"]==distinct.size()&&s["missing_permutations"]==factorial-distinct.size()&&s["repeated_permutation_windows"]==clean-distinct.size()&&s["valid_superpermutation"]==(distinct.size()==factorial),"permutation counts");
}
int main(int argc,char** argv) try {
#ifdef _WIN32
    if(argc>1) {
        for(auto args:std::vector<std::vector<std::string>>{{"-z","10"},{"-z","-m","11"}}) {
            auto r=run("0123456789A",args);
            check(r.status==1&&r.out.empty()&&r.err.find("rerun with -s")!=std::string::npos,"detached consent refusal");
        }
        return 0;
    }
    std::string command="\""+std::string(argv[0])+"\" --detached-consent";
    STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,DETACHED_PROCESS,nullptr,nullptr,&startup,&process)!=0,"start detached consent check");
    auto waited=WaitForSingleObject(process.hProcess,5000);
    if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);
    DWORD status=1;GetExitCodeProcess(process.hProcess,&status);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(waited==WAIT_OBJECT_0&&status==0,"detached consent check");
#else
    (void)argc;(void)argv;
#endif
    auto s=stats("123121321",3);check(s["length"]==9&&s["windows"]==7&&s["dirty"]==1&&s["clean"]==6&&s["max_dirty_run"]==1&&s["max_clean_run"]==3&&s["dirty_runs"]==1&&s["clean_runs"]==2&&s["valid_superpermutation"]==1,"known word");
    check(stats("1 23\r\n121\t321",3)==s,"whitespace");check(stats("012010210",3,true)==s,"zero labels");
    std::mt19937 random(42);
    for(bool zero:{false,true})for(unsigned n=1;n<=6;++n){std::string alphabet=(zero?"012345":"123456");alphabet.resize(n);oracle("",n,zero);oracle(alphabet.substr(0,n-1),n,zero);oracle(alphabet+alphabet+alphabet,n,zero);for(int trial=0;trial<5;++trial){std::string word;for(int i=0;i<(zero?150:80);++i)word+=alphabet[random()%n];oracle(word,n,zero);}}
    for(unsigned n:{10u,11u,12u,13u})for(bool zero:{false,true})oracle(std::string(zero?"0123456789ABC":"123456789ABCD").substr(0,n),n,zero);
    std::string clean;for(int i=0;i<40000;++i)clean+="12";auto large=stats(clean,2);check(large["clean"]==79999&&large["distinct_permutations"]==2&&large["repeated_permutation_windows"]==79997,"buffer boundary");
    std::string depth;for(int i=0;i<250;++i)depth+="12";depth+="11";auto longrun=run(depth,{"2"});check(longrun.out.find("[500:1]!")!=std::string::npos&&stats(depth,2)["max_clean_run"]==500,"long depth remains diagnostic");
    check(run("123121321",{"-m","3"}).out.find("[3:1]")!=std::string::npos,"codec nearest offset");
    check(run("121111112",{"3"}).out.find("[0:-4]")!=std::string::npos,"signed decimal negative offset");
    auto distant=run("12111111111112",{"3"});check(distant.status==0&&distant.err.empty()&&distant.out.find("[0:-9]")!=std::string::npos,"eight-bit distant occurrence");
    auto minimum=run("12"+std::string(130,'1')+"2",{"3"});check(minimum.err.empty()&&minimum.out.find("[0:-128]")!=std::string::npos,"eight-bit minimum signed offset");
    auto outside=run("12"+std::string(131,'1')+"2",{"3"});check(!outside.err.empty()&&outside.out.find("[0:?]!")!=std::string::npos,"offset does not wrap outside eight bits");
    check(run("1112",{"4"}).out.find("[0:?]!")!=std::string::npos,"missing copy reference");
    check(run("11111",{"3"}).out.find("[0:2] [0:2] [0:2]")!=std::string::npos,"logical tuples retain repeated pairs");
    for(auto [word,painted,n,zero]:std::vector<std::tuple<std::string,std::string,unsigned,bool>>{{"123121321","12312[1]321",3,false},{"012010210","01201[0]210",3,true},{"1111","11[1][1]",3,false},{"123","123",3,false},{"","",3,false}}){
        std::vector<std::string> args={std::to_string(n)};if(zero)args.push_back("-z");auto normal=run(word,args);args.push_back("-m");auto bracket=run(word,args);check(bracket.status==0&&bracket.err.empty()&&bracket.out.substr(0,bracket.out.find('\n'))==painted&&bracket.out.find('\033')==std::string::npos,"bracket painting");check(bracket.out.substr(bracket.out.find('\n'))==normal.out.substr(normal.out.find('\n')),"paint statistics agreement");args.push_back("-s");auto short_mode=run(word,args);check(bracket.out.substr(bracket.out.find("statistics:\n")+12)==short_mode.out,"short statistics agreement");
    }
    for(unsigned n=2;n<=9;++n) {
        std::string alphabet="123456789"; alphabet.resize(n);
        std::string word;
        for(int i=0;i<20;++i) { std::shuffle(alphabet.begin(),alphabet.end(),random); word+=alphabet+alphabet+alphabet; word+='1'; }
        oracle(word,n); // rotations, duplicate classes, and dirty/clean seams
    }
    for(auto args:std::vector<std::vector<std::string>>{{},{"0"},{"15"},{"3foo"},{"3","--unknown"},{"-s","-s","3"},{"-z","--zero-based","3"},{"-m","-m","3"},{"--bits","3"},{"--bits=5","3"},{"--bits=4","--bits=6","3"}})check(run("123",args).status!=0,"invalid arguments");
    for(auto word:{"124","120","12x"}){auto r=run(word,{"-m","3"});check(r.status!=0&&r.out.empty()&&!r.err.empty(),"invalid input");}
    check(run("",{"--help"}).status==0,"help");
    std::cout<<"Paint: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"paint test: "<<e.what()<<'\n';return 1;}
