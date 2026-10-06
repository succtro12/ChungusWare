#include "shader_contract.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv){
    if(argc!=2)return 2;
    unsigned files=0,valid=0,roles[4]{},compatible=0;
    for(auto& e:std::filesystem::directory_iterator(argv[1]))if(e.path().extension()==".dxbc"){
        std::ifstream f(e.path(),std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
        auto p=shader_contract::inspect(bytes.data(),bytes.size());
        auto c=shader_contract::classify(p);++files;valid+=p.valid;++roles[unsigned(c.role)];compatible+=c.compatible;
        if(c.role==shader_contract::Role::combine&&c.compatible){auto unsafe=p;unsafe.bindings.push_back({8,14,0,0,12,0});if(shader_contract::classify(unsafe).compatible){std::cerr<<"unknown writable combine binding accepted\n";return 1;}}
        if(c.role==shader_contract::Role::combine&&c.compatible){auto ambiguous=p;if(c.lightingSrvSpace){ambiguous.bindings.push_back({6,3,0,7,2,0});ambiguous.bindings.push_back({6,3,8,11,2,0});}else {ambiguous.bindings.push_back({3,8,0,7,2,0});ambiguous.bindings.push_back({3,8,8,11,2,0});}if(shader_contract::classify(ambiguous).compatible){std::cerr<<"ambiguous lighting contracts accepted\n";return 1;}}
        if(p.valid&&c.role==shader_contract::Role::none){auto named=p;named.entry="FinalCombine";if(shader_contract::classify(named).compatible){std::cerr<<"entry name alone accepted\n";return 1;}}
        for(size_t n=0;n<bytes.size();n+=127)if(shader_contract::inspect(bytes.data(),n).valid){std::cerr<<"truncated container accepted\n";return 1;}
        if(p.valid&&shader_contract::fingerprint(p)!=shader_contract::fingerprint(shader_contract::inspect(bytes.data(),bytes.size())))return 1;
        if(c.role!=shader_contract::Role::none)std::cout<<e.path().stem().string()<<" "<<p.entry<<" role="<<unsigned(c.role)<<" compatible="<<c.compatible<<" reason="<<c.reason<<"\n";
    }
    if(!files||!valid||!roles[1]||!roles[2]||!roles[3])return 1;
    std::cout<<"PASS containers="<<files<<" valid="<<valid<<" diffuse="<<roles[1]<<" specular="<<roles[2]<<" combine="<<roles[3]<<" compatible="<<compatible<<"; truncated containers rejected\n";
}
