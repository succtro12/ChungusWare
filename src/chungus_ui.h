#pragma once
#include <array>
#include <string>
#include <vector>
namespace chungus_ui {
std::string wrapHauntedCaption(const std::string&);
struct Rect {float x,y,w,h;bool contains(float px,float py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
using Color=std::array<float,4>;
struct Quad {Rect rect,uv,clip;Color top,bottom;};
struct Input {float x=0,y=0;bool down=false,pressed=false,released=false,tab=false,shift=false,enter=false,up=false,below=false,left=false,right=false;double time=0;};
struct Model {
    int resolution=2,renderer=2,fg=0,maximumFg=3,pacing=0,domain=0;
    float target=0;bool remember=false;
    bool rrActive=false,reflex=false;std::string dimensions="waiting for an rtx world",status="waiting for a world frame";
    std::vector<std::string> diagnostics;
    unsigned rabbitTier=0;
    std::string rabbitMessage="inventory complete.";
};
struct Actions {bool apply=false,close=false;};
class Panel {
public:
    Actions frame(Model&,const Input&,unsigned width,unsigned height);
    const std::vector<Quad>& quads()const{return commands;}
    void resetInteraction(){popup=0;pressedId=0;drag=false;}
    bool advanced()const{return page!=0;}
private:
    std::vector<Quad> commands;Input input;float scale=1,originX=42,originY=42;unsigned screenW=0,screenH=0;
    Rect clip{0,0,720,610};int id=0,focus=1,previousCount=1,pressedId=0,popup=0,page=0;
    bool drag=false;float dragX=0,dragY=0;int hovered=0,lastHovered=0;double hoverSince=0;
    bool darkTip=false;std::string tip;Rect popupRect{};std::vector<std::string> popupItems;int* popupValue=nullptr;
    void quad(Rect,Color,Color,Rect uv={1023.f/1024,1023.f/1024,1.f/1024,1.f/1024});
    void fill(Rect,Color);void bevel(Rect,bool inset=false,Color top={.97f,.78f,.43f,1},Color bottom={.78f,.55f,.28f,1});
    void text(float,float,const std::string&,Color color={.19f,.12f,.06f,1},float size=1);
    void group(Rect,const char*);void light(float,float,bool);bool hit(int,Rect,const char*,bool disabled=false);
    bool button(Rect,const char*,const char* tooltip="",bool important=false,bool disabled=false);
    bool check(Rect,const char*,bool&,const char* tooltip="",bool disabled=false,bool darkTooltip=false);
    void combo(Rect,int&,const std::vector<std::string>&,const char* tooltip);
    void finishPopup();void finishTooltip();
};
}
