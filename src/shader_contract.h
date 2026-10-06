#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <span>

// Read-only DXIL PSV metadata. Never authorizes a dispatch from its name alone.
// Layout: Microsoft's DxilPipelineStateValidation.h, PSVRuntimeInfo3 and
// PSVResourceBindInfo1. Reject other versions instead of guessing their layout.
namespace shader_contract {
struct Binding { uint32_t type,space,first,last,kind,flags; };
struct Profile {
    bool valid=false;
    std::string entry;
    uint32_t threads[3]{};
    std::vector<Binding> bindings;
};
inline Profile inspect(const void* bytes,size_t length) {
    Profile p;
    if(!bytes||length<32||length>64*1024*1024)return p;
    auto b=std::span(static_cast<const uint8_t*>(bytes),length);
    auto read=[](std::span<const uint8_t> s,size_t at,uint32_t& v){
        if(at>s.size()||s.size()-at<4)return false;
        std::memcpy(&v,s.data()+at,4);return true;
    };
    uint32_t total=0,count=0;
    if(std::memcmp(b.data(),"DXBC",4)||!read(b,24,total)||total!=length||
       !read(b,28,count)||count>(length-32)/4)return p;
    for(uint32_t i=0;i<count;i++) {
        uint32_t off=0,size=0;
        if(!read(b,32+size_t(i)*4,off)||!read(b,size_t(off)+4,size)||
           off>length||length-off<8||size>length-off-8)return {};
        if(std::memcmp(b.data()+off,"PSV0",4))continue;
        auto s=b.subspan(size_t(off)+8,size);
        uint32_t info=0,resources=0,stride=0,nameOffset=0;
        if(!read(s,0,info)||(info!=48&&info!=52)||s.size()<4+info||s[28]!=5||
           !read(s,40,p.threads[0])||!read(s,44,p.threads[1])||
           !read(s,48,p.threads[2])||
           (info==52&&!read(s,52,nameOffset))||
           !read(s,4+info,resources)||resources>4096)return {};
        size_t cursor=8+info;
        if(resources) {
            if(!read(s,cursor,stride)||stride!=24)return {};
            cursor+=4;
            if(cursor>s.size()||resources>(s.size()-cursor)/stride)return {};
            for(uint32_t r=0;r<resources;r++) {
                Binding binding{};
                std::memcpy(&binding,s.data()+cursor,24);
                if(binding.first>binding.last)return {};
                p.bindings.push_back(binding);cursor+=stride;
            }
        }
        if(info==48){p.valid=true;return p;}
        uint32_t stringBytes=0;
        if(!read(s,cursor,stringBytes))return {};
        cursor+=4;
        if(cursor>s.size()||stringBytes>s.size()-cursor||nameOffset>=stringBytes)return {};
        auto name=s.subspan(cursor+nameOffset,stringBytes-nameOffset);
        for(uint8_t c:name) {
            if(!c){p.valid=!p.entry.empty();return p;}
            // Bound and sanitize logging; entry names are diagnostic data.
            if(p.entry.size()>=127||!(c=='_'||(c>='a'&&c<='z')||
               (c>='A'&&c<='Z')||(c>='0'&&c<='9')))return {};
            p.entry.push_back(char(c));
        }
        return {};
    }
    return {};
}
enum class Role { none, diffuse, specular, combine };
struct Candidate { Role role=Role::none; bool compatible=false; const char* reason="unclassified"; unsigned lightingSrvSpace=0; bool auxiliaryBuffer=false; };
inline bool has(const Profile& p,uint32_t type,uint32_t reg,uint32_t space=0) {
    for(auto& b:p.bindings)if(b.type==type&&b.space==space&&b.first<=reg&&reg<=b.last)return true;
    return false;
}
inline uint64_t fingerprint(const Profile& p) {
    uint64_t h=14695981039346656037ull;
    auto feed=[&](uint32_t v){for(unsigned i=0;i<4;i++){h^=uint8_t(v>>(8*i));h*=1099511628211ull;}};
    for(auto v:p.threads)feed(v);
    // PSV preserves declaration order. A reordered profile is revalidated.
    for(auto& b:p.bindings){feed(b.type);feed(b.space);feed(b.first);feed(b.last);feed(b.kind);feed(b.flags);}
    return h;
}
inline uint64_t canonical(Role r) {
    return r==Role::diffuse?0x17e950ce2bdc6a38ull:r==Role::specular?0xe91e519842717ad4ull:
           r==Role::combine?0xa2c049a5a3f59ee4ull:0;
}
inline Candidate classify(const Profile& p) {
    if(!p.valid||!has(p,2,0)||!has(p,2,2))return {};
    bool rays=p.threads[0]==4&&p.threads[1]==8&&p.threads[2]==1&&
        has(p,4,0)&&has(p,3,14)&&has(p,3,15)&&has(p,5,19)&&has(p,6,4);
    bool diffuse=rays&&has(p,6,5)&&has(p,6,28);
    bool specular=rays&&has(p,6,6)&&has(p,6,15);
    bool combine=p.threads[0]==16&&p.threads[1]==16&&p.threads[2]==1;
    for(auto reg:{14u,15u,16u,21u,44u,46u})combine=combine&&has(p,3,reg);
    for(auto reg:{12u,16u,17u,18u,23u})combine=combine&&has(p,6,reg);
    unsigned matches=unsigned(diffuse)+unsigned(specular)+unsigned(combine);
    if(matches!=1)return {Role::none,false,matches?"ambiguous pass contracts":"no pass contract"};
    auto role=diffuse?Role::diffuse:specular?Role::specular:Role::combine;
    bool legacyLighting=has(p,6,0,3)&&has(p,6,8,3);
    bool srvLighting=has(p,3,0,8)&&has(p,3,8,8);
    if(combine&&legacyLighting==srvLighting)
        return {role,false,"combine lighting contract missing or ambiguous"};
    bool auxiliary=false;
    if(combine)for(auto& b:p.bindings)if(b.type>=3&&b.type<=5){
        bool allowed=false;
        if(b.space==0&&b.first==b.last)for(unsigned reg:{11u,12u,14u,15u,16u,17u,19u,21u,38u,44u,46u,47u,50u,59u})if(b.first==reg)allowed=true;
        if((b.space==4&&b.first==0&&b.last==1)||(b.space==6&&b.first==0&&b.last==4095))allowed=true;
        if(srvLighting&&b.type==3&&b.space==8&&((b.first==0&&b.last==7)||(b.first==8&&b.last==11)))allowed=true;
        if(!allowed)return {role,false,"combine has a read binding outside the descriptor clone contract"};
    }
    for(auto& b:p.bindings)if(b.type>=6&&b.type<=9) {
        if(combine) {
            bool outputs=b.space==0&&b.first==b.last&&
                (b.first==12||b.first==14||b.first==16||b.first==17||b.first==18||b.first==23);
            bool inputs=b.space==3&&b.type==6&&b.first<=11&&b.last<=11;
            bool ownedAux=srvLighting&&!auxiliary&&b.type==8&&b.space==14&&b.first==0&&b.last==0;
            if(ownedAux)auxiliary=true;
            if(!outputs&&!inputs&&!ownedAux)return {role,false,"combine has an unredirected writable binding"};
        } else if(b.space==0&&(b.first!=b.last||
                  (b.first!=4&&b.first!=14&&b.first!=5&&b.first!=6&&b.first!=15&&b.first!=28)))
            return {role,false,"raw pass has unknown output bindings"};
    }
    return {role,true,"resource contract candidate; requires runtime validation",combine&&srvLighting?8u:0u,auxiliary};
}
}
