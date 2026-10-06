#pragma once
#include <algorithm>
#include <cmath>
namespace cw_resolution {
constexpr int FraudulentDlaa=100, Caca=101, Custom=102;
inline bool valid(int mode){return mode==-1||mode==0||mode==1||mode==2||mode==3||mode==FraudulentDlaa||mode==Caca||mode==Custom;}
inline int quality(int mode){return mode==Caca?3:mode>=100?2:mode;}
inline float scale(int mode,unsigned height,float custom){
    switch(mode){case 0:return .5f;case 1:return .58f;case 2:return 2.f/3.f;case 3:return 1.f/3.f;case FraudulentDlaa:return .999f;case Caca:return height?std::min(.999f,144.f/height):.1f;case Custom:return std::clamp(custom,.05f,.999f);default:return 1.f;}
}
struct Size{unsigned width,height;};
inline Size dimensions(int mode,unsigned width,unsigned height,float custom){
    if(width<2||height<2)return {width,height};
    const float ratio=scale(mode,height,custom);
    return {std::clamp(unsigned(std::lround(width*ratio)),1u,width-1),std::clamp(unsigned(std::lround(height*ratio)),1u,height-1)};
}
}
