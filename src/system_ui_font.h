#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include "ui_font_metrics.h"

namespace chungus_ui {
struct LocalUiFont {
    std::array<float,95> spacing{};
    std::vector<unsigned char> glyphs;
    std::string status="Tahoma unavailable; bundled W95FA fallback";
};
inline int CALLBACK foundLocalFace(const LOGFONTW* font,const TEXTMETRICW*,DWORD type,LPARAM found) {
    if((type&TRUETYPE_FONTTYPE)&&_wcsicmp(font->lfFaceName,L"Tahoma")==0){*reinterpret_cast<bool*>(found)=true;return 0;}return 1;
}
// GDI uses the locally installed face. No font bytes or generated glyphs are
// read from the package, written to disk, cached persistently or downloaded.
inline bool rasterizeLocalTahoma(LocalUiFont& out) {
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;
    bool found=false;LOGFONTW query{};query.lfCharSet=DEFAULT_CHARSET;wcscpy_s(query.lfFaceName,L"Tahoma");
    EnumFontFamiliesExW(dc,&query,foundLocalFace,reinterpret_cast<LPARAM>(&found),0);
    if(!found){DeleteDC(dc);return false;}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=1024;info.bmiHeader.biHeight=-420;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP dib=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dib||!bits){if(dib)DeleteObject(dib);DeleteDC(dc);return false;}
    HGDIOBJ originalBitmap=SelectObject(dc,dib);std::memset(bits,0,1024*420*4);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    auto spacing=out.spacing;bool ok=true;const int sizes[]={15,18,12,13,16,11};
    for(int role=0;role<6&&ok;role++) {
        LOGFONTW description{};description.lfHeight=-sizes[role];description.lfWeight=FW_NORMAL;description.lfCharSet=DEFAULT_CHARSET;description.lfQuality=ANTIALIASED_QUALITY;wcscpy_s(description.lfFaceName,L"Tahoma");
        HFONT font=CreateFontIndirectW(&description);if(!font){ok=false;break;}
        HGDIOBJ oldFont=SelectObject(dc,font);wchar_t selected[LF_FACESIZE]{};
        if(!oldFont||oldFont==HGDI_ERROR||!GetTextFaceW(dc,LF_FACESIZE,selected)||_wcsicmp(selected,L"Tahoma")!=0)ok=false;
        for(int i=0;i<95&&ok;i++) {
            wchar_t c=wchar_t(i+32);SIZE extent{};if(!GetTextExtentPoint32W(dc,&c,1,&extent)){ok=false;break;}
            if(role==5){spacing[i]=float(extent.cx);continue;}
            if(extent.cx>24||extent.cy>27){ok=false;break;}
            int x=i%42*24,y=role*84+i/42*28;int saved=SaveDC(dc);if(!saved){ok=false;break;}
            IntersectClipRect(dc,x,y,x+24,y+28);ok=TextOutW(dc,x,y+2,&c,1)!=FALSE;RestoreDC(dc,saved);
        }
        if(oldFont&&oldFont!=HGDI_ERROR)SelectObject(dc,oldFont);DeleteObject(font);
    }
    if(ok)ok=GdiFlush()!=FALSE;
    std::vector<unsigned char> glyphs;
    if(ok) {
        glyphs.resize(1024*420*4);auto raw=static_cast<unsigned char*>(bits);
        for(size_t i=0;i<glyphs.size();i+=4){glyphs[i]=glyphs[i+1]=glyphs[i+2]=255;glyphs[i+3]=std::max({raw[i],raw[i+1],raw[i+2]});}
    }
    SelectObject(dc,originalBitmap);DeleteObject(dib);DeleteDC(dc);
    if(!ok)return false;out.spacing=spacing;out.glyphs=std::move(glyphs);out.status="local Windows Tahoma; memory-only glyphs";return true;
}
inline const LocalUiFont& localUiFont() {
    static const LocalUiFont font=[] {
        LocalUiFont f;std::copy(std::begin(advances),std::end(advances),f.spacing.begin());
        try{rasterizeLocalTahoma(f);}catch(...){f.glyphs.clear();std::copy(std::begin(advances),std::end(advances),f.spacing.begin());f.status="Tahoma rasterization failed; bundled W95FA fallback";}
        OutputDebugStringA(("ChungusWare UI font: "+f.status+"\n").c_str());return f;
    }();return font;
}
inline const std::array<float,95>& uiFontSpacing(){return localUiFont().spacing;}
inline bool applyUiFontGlyphs(const LocalUiFont& f,std::vector<unsigned char>& atlas) {
    if(f.glyphs.size()!=1024*420*4||atlas.size()!=1024*1024*4)return false;
    std::copy(f.glyphs.begin(),f.glyphs.end(),atlas.begin());return true;
}
inline bool applyLocalUiFont(std::vector<unsigned char>& atlas){return applyUiFontGlyphs(localUiFont(),atlas);}
}
