// SPDX-License-Identifier: GPL-3.0-or-later
// Lazy singleton-expression profile of Beesley's Kleene language. See KLEENE.md.
#include <cctype>
#include <sstream>

struct KlSource {
    std::istream &in;
    std::string token;
    std::unordered_map<std::string,std::string> metadata;
    std::unordered_map<std::string,Id> names;
    Grammar grammar;
    size_t consumed=0, line=1;
    bool code=false;
    explicit KlSource(std::istream &source):in(source) {}
    [[noreturn]] void error(const std::string &s) { fail("Kleene line "+std::to_string(line)+": "+s); }
    int take() {
        int c=in.get();
        if(c==EOF) { if(in.bad()) error("input read failed"); return c; }
        if(++consumed>256u*1024u*1024u) error("source exceeds 256 MiB budget");
        if(c=='\n') ++line;
        return c;
    }
    static bool letter(int c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || c=='_'; }
    static bool digit(int c) { return c>='0'&&c<='9'; }
    void comment(const std::string &text) {
        std::istringstream words(text); std::string marker,key;
        if(!(words>>marker) || marker!="@spra") return;
        if(code) error("SPRA metadata must precede Kleene statements");
        if(!(words>>key) || (key!="version" && key!="n" && key!="length")) error("unknown SPRA metadata field");
        std::string value; std::getline(words,value);
        if(!metadata.emplace(key,value).second) error("duplicate SPRA metadata field "+key);
    }
    void next() {
        token.clear();
        for(;;) {
            int c=in.peek();
            if(c==EOF) { if(in.bad()) error("input read failed"); return; }
            if(c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f') { take(); continue; }
            if(c!='/') break;
            take(); c=take();
            if(c=='/') {
                std::string text;
                while(in.peek()!=EOF && in.peek()!='\n' && in.peek()!='\r') {
                    text.push_back(char(take()));
                    if(text.size()>65536) error("comment line exceeds budget");
                }
                comment(text);
            } else if(c=='*') {
                unsigned nesting=1;
                while(nesting) {
                    c=take(); if(c==EOF) error("unterminated block comment");
                    if(c=='/' && in.peek()=='*') { take(); if(++nesting>256) error("comment nesting exceeds budget"); }
                    else if(c=='*' && in.peek()=='/') { take(); --nesting; }
                }
            } else error("unsupported syntax; expected a comment after /");
        }
        code=true;
        int c=take(); token.push_back(char(c));
        if(c=='$') {
            if(!letter(in.peek())) error("expected a network variable such as $word");
            while(letter(in.peek())||digit(in.peek())) { token.push_back(char(take())); if(token.size()>128) error("variable name exceeds budget"); }
        } else if(c=='\'') {
            while(in.peek()!='\'') {
                c=take(); if(c==EOF||c=='\n'||c=='\r'||c=='\\') error("expected a single-quoted tuple symbol");
                token.push_back(char(c)); if(token.size()>128) error("tuple symbol exceeds budget");
            }
            token.push_back(char(take()));
        } else if(letter(c)||digit(c)) {
            while(letter(in.peek())||digit(in.peek())) { token.push_back(char(take())); if(token.size()>128) error("token exceeds budget"); }
        } else if(c!='='&&c!=';'&&c!='('&&c!=')'&&c!='{'&&c!='}') {
            error("operator is outside the finite singleton Kleene profile");
        }
    }
    void expect(const std::string &s) { if(token!=s) error("expected "+s); next(); }
    U integer(const std::string &s) {
        U x=0; auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),x);
        if(s.empty()||ec!=std::errc{}||end!=s.data()+s.size()) error("expected an unsigned 64-bit integer");
        return x;
    }
    std::string field(const std::string &key) {
        auto it=metadata.find(key); if(it==metadata.end()) error("missing @spra "+key);
        return it->second;
    }
    std::string single(const std::string &key) {
        std::istringstream words(field(key)); std::string s,extra;
        if(!(words>>s) || words>>extra) error("expected one value for @spra "+key);
        return s;
    }
    Header header() {
        if(integer(single("version"))!=1) error("unsupported SPRA recipe profile version");
        U n=integer(single("n")), length=integer(single("length"));
        if(n<1||n>32) error("N outside SPRA range 1..32");
        Header h; h.n=unsigned(n); h.length=length;
        h.data={'S','P','R','A',2,0x44,0,static_cast<unsigned char>(n)};
        for(unsigned j=0;j<8;++j) h.data.push_back(static_cast<unsigned char>(length>>(8*j)));
        h.data.resize(24,0);
        for(U j=0;j<std::min<U>(length,n-1);++j) h.data.push_back(static_cast<unsigned char>(j));
        return h;
    }
    unsigned n=0;
    bool primary() { return !token.empty() && (token[0]=='$'||token[0]=='\''||token=="("||token=="_e_"); }
    Id atom() {
        // Symbol '[depth,offset]' is ONE Kleene alphabet symbol, not text bytes.
        const std::string s=token;
        if(s.size()<7||s[1]!='['||s[s.size()-2]!=']') error("tuple symbol must be '[depth,offset]'");
        auto comma=s.find(',',2); if(comma==std::string::npos) error("tuple symbol needs a comma");
        U depth=integer(s.substr(2,comma-2));
        std::string offset=s.substr(comma+1,s.size()-comma-3);
        if(!offset.empty()&&offset[0]=='+') offset.erase(0,1);
        int o=0; auto [end,ec]=std::from_chars(offset.data(),offset.data()+offset.size(),o);
        if(offset.empty()||ec!=std::errc{}||end!=offset.data()+offset.size()||depth>63||o < -32||o>31||o>=int(n)||(depth&&o==0)) error("tuple outside SPRA logical field ranges");
        Id id=grammar.atom(unsigned(depth),o); next(); return id;
    }
    Id expression(unsigned nesting=0) {
        if(nesting>256) error("expression nesting exceeds budget");
        if(!primary()) error("expected tuple, variable, parentheses or _e_");
        Builder builder(grammar);
        bool nonempty=false;
        while(primary()) {
            Id id=0;
            if(token[0]=='\'') id=atom();
            else if(token[0]=='$') {
                auto it=names.find(token); if(it==names.end()) error("undefined or forward network reference "+token);
                id=it->second; next();
            } else if(token=="_e_") { id=grammar.concat({}); next(); }
            else { next(); id=expression(nesting+1); expect(")"); }
            while(token=="{") {
                next(); U count=integer(token);
                if(count>U(INT64_MAX)) error("Kleene repetition literal exceeds signed 64-bit range; use nested repetitions");
                next(); expect("}");
                id=count&&grammar.nodes[id].length?grammar.repeat(id,count):grammar.concat({});
            }
            if(grammar.nodes[id].length) { builder.push(id); nonempty=true; }
        }
        return nonempty?builder.finish():grammar.concat({});
    }
    std::pair<Header,Grammar> read(Id &root) {
        next(); Header h=header(); n=h.n;
        bool found=false;
        while(!token.empty()) {
            if(found) error("$word must be the final definition");
            if(token[0]!='$') error("only network assignments are supported in the finite singleton profile");
            std::string name=token;
            if(names.contains(name)) error("network reassignment is outside this profile");
            if(names.size()>=MAX_NODES) error("definition budget exceeded");
            next(); expect("="); Id id=expression(); expect(";"); names.emplace(name,id);
            if(name=="$word") { found=true; root=id; }
        }
        if(!found) error("missing final $word definition");
        h.tuples=grammar.nodes[root].length; h.bits=4;
        std::vector<bool> used(grammar.nodes.size()); std::vector<Id> todo{root};
        while(!todo.empty()) {
            Id id=todo.back(); todo.pop_back(); if(used[id]) continue; used[id]=true;
            const Node &x=grammar.nodes[id];
            if(x.kind=='t'&&(x.depth>15||x.offset < -8||x.offset>7)) h.bits=6;
            for(Id child:x.children) todo.push_back(child);
        }
        std::vector<U> clean(grammar.nodes.size());
        for(size_t i=0;i<grammar.nodes.size();++i) {
            if(!used[i]) continue;
            const Node &x=grammar.nodes[i];
            if(x.kind=='t') clean[i]=x.depth;
            else if(x.kind=='r') clean[i]=mul(clean[x.children[0]],x.count);
            else for(Id child:x.children) clean[i]=add(clean[i],clean[child]);
        }
        U covered=add(std::min<U>(h.length,n-1),add(h.tuples,clean[root]));
        if(covered>h.length) error("tuple actions exceed declared length");
        h.data[5]=static_cast<unsigned char>((h.bits<<4)|h.bits);
        for(unsigned j=0;j<8;++j) h.data[16+j]=static_cast<unsigned char>(h.tuples>>(8*j));
        h.check();
        grammar.index.clear(); grammar.index.rehash(0);
        return {std::move(h),std::move(grammar)};
    }
};

void write_kl(const Header &h,const Grammar &g,Id root) {
    // Tuple depths/copy distances are invariant under a bijective relabeling.
    // Normalize a distinct source seed to 0,1,... and use one-based output.
    std::array<bool,32> seen{};
    for(size_t i=24;i<h.data.size();++i) {
        if(seen[h.data[i]]) fail("cannot normalize a repeated-symbol seed to the identity seed");
        seen[h.data[i]]=true;
    }
    std::vector<bool> used(g.nodes.size()); std::vector<Id> todo{root};
    while(!todo.empty()) { Id id=todo.back(); todo.pop_back(); if(used[id]) continue;
        used[id]=true; for(Id c:g.nodes[id].children) todo.push_back(c); }
    std::vector<Id> remap(g.nodes.size()); Id count=0;
    for(size_t i=0;i<used.size();++i) if(used[i]) remap[i]=count++;
    std::cout<<"// @spra version 1\n// @spra n "<<h.n<<"\n// @spra length "<<h.length<<'\n';
    for(size_t i=0;i<g.nodes.size();++i) if(used[i]) {
        const Node &x=g.nodes[i]; std::cout<<"$r"<<remap[i]<<" = ";
        if(x.kind=='t') std::cout<<"'["<<x.depth<<','<<(x.offset>=0?"+":"")<<x.offset<<"]'";
        else if(x.kind=='r') {
            Id child=remap[x.children[0]];
            if(x.count<=U(INT64_MAX)) std::cout<<"$r"<<child<<'{'<<x.count<<'}';
            else {
                std::cout<<"($r"<<child<<'{'<<INT64_MAX<<"}){"<<x.count/U(INT64_MAX)<<'}';
                if(U rest=x.count%U(INT64_MAX)) std::cout<<" $r"<<child<<'{'<<rest<<'}';
            }
        }
        else if(x.children.empty()) std::cout<<"_e_";
        else { bool first=true; for(Id c:x.children) { if(!first) std::cout<<' '; first=false; std::cout<<"$r"<<remap[c]; } }
        std::cout<<";\n";
    }
    std::cout<<"$word = $r"<<remap[root]<<";\n"; std::cout.flush();
    if(!std::cout) fail("Kleene source write failed");
}
