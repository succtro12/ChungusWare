#include "local_ui_assets.h"
#include <iostream>
#include <fstream>
int wmain(int argc,wchar_t** argv){
    std::vector<unsigned char> atlas(1024*1024*4,17);std::string status;
    bool loaded=chungus_ui::loadContainment(GetModuleHandleW(nullptr),atlas,status);
    bool expected=argc>1&&std::wstring(argv[1])==L"present";
    if(loaded!=expected){std::cerr<<status<<'\n';return 1;}
    auto sound=chungus_ui::localAsset(GetModuleHandleW(nullptr),L"caca_impact.wav");
    if(!std::filesystem::is_regular_file(sound)){std::cerr<<"module-local sound missing\n";return 2;}
    if(!loaded&&std::any_of(atlas.begin(),atlas.end(),[](unsigned char b){return b!=17;})){std::cerr<<"failed optional image changed atlas\n";return 3;}
    // Glyph rows and white texel must survive optional image replacement.
    if(atlas[0]!=17||atlas.back()!=17){std::cerr<<"glyph region overwritten\n";return 4;}
    std::cout<<status<<"; module-local impact found\n";return 0;
}
