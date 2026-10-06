#include "system_ui_font.h"
#include <iostream>
#include <stdexcept>
int main(){try {
    const auto& font=chungus_ui::localUiFont();
    if(font.glyphs.empty())throw std::runtime_error("Installed Tahoma not selected on this test machine");
    std::vector<unsigned char> atlas(1024*1024*4,17);
    if(!chungus_ui::applyLocalUiFont(atlas))throw std::runtime_error("Font overlay failed");
    if(atlas[448*1024*4]!=17||atlas.back()!=17)throw std::runtime_error("Cosmetic texture region modified");
    bool any=false;for(size_t n=3;n<font.glyphs.size();n+=4)any|=font.glyphs[n]!=0;
    if(!any)throw std::runtime_error("No glyph coverage");
    for(auto advance:font.spacing)if(advance<=0||advance>24)throw std::runtime_error("Invalid runtime spacing");
    std::vector<unsigned char> invalid(16,17);if(chungus_ui::applyLocalUiFont(invalid))throw std::runtime_error("Invalid texture accepted");
    std::vector<unsigned char> fallback(1024*1024*4,17);chungus_ui::LocalUiFont unavailable;
    if(chungus_ui::applyUiFontGlyphs(unavailable,fallback)||std::any_of(fallback.begin(),fallback.end(),[](unsigned char p){return p!=17;}))throw std::runtime_error("Unavailable font changed fallback atlas");
    DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<20;i++){chungus_ui::LocalUiFont temporary; if(!chungus_ui::rasterizeLocalTahoma(temporary))throw std::runtime_error("Repeat rasterization failed");}
    if(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)>before)throw std::runtime_error("GDI objects leaked");
    std::cout<<font.status<<"; glyphs in memory only; mascot preserved; 20 builds without GDI leaks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
