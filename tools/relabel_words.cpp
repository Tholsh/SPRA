// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Theo H.
// Binary streaming relabeler. See docs/tools.md for build and usage.
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif
namespace fs = std::filesystem;
static void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
static bool inside(const fs::path& child, const fs::path& parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c) {
        if (c == child.end()) return false;
#ifdef _WIN32
        if (CompareStringOrdinal(c->c_str(), -1, p->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
#else
        if (*c != *p) return false;
#endif
    }
    return true;
}
static void convert(FILE* in, FILE* out, int to, int n) {
    std::string source = to ? "0123456789ABC" : "123456789ABCD";
    std::string target = to ? "123456789ABCD" : "0123456789ABC";
    std::array<int,256> table; table.fill(-1);
    for (unsigned char c : std::string(" \t\r\n\v\f")) table[c] = c;
    for (int i=0; i<n; ++i) table[static_cast<unsigned char>(source[i])] = target[i];
    std::vector<unsigned char> buffer(1024*1024);
    unsigned long long offset=0;
    while (auto count=std::fread(buffer.data(),1,buffer.size(),in)) {
        for (std::size_t i=0;i<count;++i) {
            require(table[buffer[i]]>=0,"invalid source symbol at byte "+std::to_string(offset+i)+"; expected "+source.substr(0,n));
            buffer[i]=static_cast<unsigned char>(table[buffer[i]]);
        }
        require(std::fwrite(buffer.data(),1,count,out)==count,"cannot write output"); offset+=count;
    }
    require(!std::ferror(in),"cannot read input");
    require(std::fflush(out)==0,"cannot flush output");
}
static FILE* open_file(const fs::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wide; while (*mode) wide.push_back(*mode++);
    return _wfopen(path.c_str(),wide.c_str());
#else
    return std::fopen(path.c_str(),mode);
#endif
}
static void check_output(const fs::path& source,const fs::path& dest,bool overwrite) {
    require(!fs::is_symlink(fs::symlink_status(dest)),"refusing a symlink output: "+dest.string());
    require(fs::weakly_canonical(source)!=fs::weakly_canonical(dest),"input and output must be different files");
    if (fs::exists(dest)) {
        require(!fs::equivalent(source,dest),"input and output must be different files");
        require(overwrite && fs::is_regular_file(dest),"output already exists: "+dest.string()+"; use --overwrite to replace regular files");
    }
}
static void convert_file(const fs::path& source,const fs::path& dest,int to,int n,bool overwrite) {
    check_output(source,dest,overwrite);
    auto parent=fs::absolute(dest).parent_path(); fs::create_directories(parent);
    fs::path temp; FILE* out=nullptr; std::random_device random;
    for(int i=0;i<100 && !out;++i) {temp=parent/(".relabel-"+std::to_string(random())+"-"+std::to_string(random()));out=open_file(temp,"wbx");}
    require(out,"cannot create temporary output");
    FILE* in=nullptr;
    try {
        in=open_file(source,"rb"); require(in,"cannot open input: "+source.string());
        convert(in,out,to,n); std::fclose(in); in=nullptr;
        auto status=std::fclose(out); out=nullptr; require(status==0,"cannot close output");
        if(overwrite) {
#ifdef _WIN32
            require(MoveFileExW(temp.c_str(),fs::absolute(dest).c_str(),MOVEFILE_REPLACE_EXISTING)!=0,"cannot publish output");
#else
            fs::rename(temp,dest);
#endif
        } else fs::create_hard_link(temp,dest);
        std::error_code ec; fs::remove(temp,ec);
    } catch(...) {if(in)std::fclose(in);if(out)std::fclose(out);std::error_code ec;fs::remove(temp,ec);throw;}
}
int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
#endif
        int to=-1,n=13; bool recursive=false,overwrite=false,dry=false,options=true;
        std::vector<std::string> paths;
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            if(options && (a=="--help"||a=="-h")) {std::cout<<"Usage: relabel-words --to 0|1 [-n 1..13] [--recursive] [--overwrite] [--dry-run] [INPUT|-] [OUTPUT|-]\n--to 0 expects 123456789ABCD; --to 1 expects 0123456789ABC.\nWhitespace is preserved. Recursive mode converts .txt files into a separate tree.\n";return 0;}
            if(options && a=="--") {options=false;continue;}
            if(options && (a=="--to"||a=="-n"||a=="--n")) {require(i+1<argc,"missing option value");std::string v=argv[++i];require(!v.empty()&&v.find_first_not_of("0123456789")==std::string::npos,"invalid numeric option");int value=std::stoi(v);if(a=="--to")to=value;else n=value;}
            else if(options && a=="--recursive")recursive=true;
            else if(options && a=="--overwrite")overwrite=true;
            else if(options && a=="--dry-run")dry=true;
            else {require(!options||a=="-"||a.empty()||a[0]!='-',"unknown option: "+a);paths.push_back(a);}
        }
        require((to==0||to==1)&&n>=1&&n<=13,"--to 0|1 is required; N must be 1..13");
        require(paths.size()<=2,"too many paths");while(paths.size()<2)paths.push_back("-");
        if(recursive) {
            require(paths[0]!="-"&&paths[1]!="-","recursive mode requires two folders");
            auto source=fs::weakly_canonical(fs::absolute(paths[0])),dest=fs::weakly_canonical(fs::absolute(paths[1]));
            require(fs::is_directory(source),"input folder does not exist");
            require(!inside(source,dest)&&!inside(dest,source),"input and output folders must be separate, non-nested trees");
            std::vector<std::pair<fs::path,fs::path>> plan;
            for(auto it=fs::recursive_directory_iterator(source);it!=fs::recursive_directory_iterator();++it) {
                if(it->is_symlink()) {it.disable_recursion_pending();continue;}
                std::string extension=it->path().extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return std::tolower(c);});
                if(!it->is_regular_file()||extension!=".txt")continue;
                auto output=dest/it->path().lexically_relative(source);
                require(inside(fs::weakly_canonical(output),dest),"output escapes destination");
                check_output(it->path(),output,overwrite);plan.emplace_back(it->path(),output);
            }
            std::sort(plan.begin(),plan.end());
            for(auto& [input,output]:plan) {if(!dry)convert_file(input,output,to,n,overwrite);std::cerr<<input.string()<<" -> "<<output.string()<<'\n';}
            std::cerr<<(dry?"Would convert ":"Converted ")<<plan.size()<<" file(s).\n";
        } else {
            require(!dry,"--dry-run requires --recursive");
            require(paths[0]!="-"||paths[1]=="-","stdin mode requires stdout");
            if(paths[1]!="-")convert_file(paths[0],paths[1],to,n,overwrite);
            else {FILE* in=paths[0]=="-"?stdin:open_file(paths[0],"rb");require(in,"cannot open input");try{convert(in,stdout,to,n);}catch(...){if(in!=stdin)std::fclose(in);throw;}if(in!=stdin)std::fclose(in);}
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<"relabel: "<<e.what()<<'\n';return 1;}
}
