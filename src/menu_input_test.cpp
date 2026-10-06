#include "menu_input.h"
#include <iostream>
#include <stdexcept>
void require(bool condition){if(!condition)throw std::runtime_error("Menu input regression");}
UINT WINAPI fakeRaw(HRAWINPUT,UINT,LPVOID data,PUINT,UINT){if(data){auto r=static_cast<RAWINPUT*>(data);*r={};r->header.dwType=RIM_TYPEMOUSE;r->data.mouse.lLastX=17;r->data.mouse.lLastY=-9;r->data.mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_RIGHT_BUTTON_UP;}return sizeof(RAWINPUT);}
LRESULT WINAPI fakeDispatch(const MSG*){return 123;}
int main(){try{
    menu_input::rawInput=&fakeRaw;menu_input::dispatch=&fakeDispatch;
    RAWINPUT data{};UINT size=sizeof(data);
    menu_input::setActive(false);menu_input::readRaw(nullptr,RID_INPUT,&data,&size,sizeof(RAWINPUTHEADER));require(data.data.mouse.lLastX==17);
    menu_input::setActive(true);menu_input::readRaw(nullptr,RID_INPUT,&data,&size,sizeof(RAWINPUTHEADER));require(!data.data.mouse.lLastX&&!data.data.mouse.lLastY&&data.data.mouse.usButtonFlags==RI_MOUSE_RIGHT_BUTTON_UP);
    menu_input::readRaw(nullptr,RID_HEADER,&data,&size,sizeof(RAWINPUTHEADER));require(data.data.mouse.lLastX==17);
    data={};data.header.dwType=RIM_TYPEKEYBOARD;data.data.keyboard.VKey='W';menu_input::filterRaw(&data,sizeof(data));require(data.data.keyboard.VKey==0&&(data.data.keyboard.Flags&RI_KEY_BREAK));
    data.data.keyboard.VKey='W';menu_input::filterRaw(&data,sizeof(data));require(data.data.keyboard.VKey=='W');
    MSG msg{};msg.message=WM_MOUSEMOVE;require(menu_input::dispatchMessage(&msg)==0);msg.message=WM_KEYDOWN;require(menu_input::dispatchMessage(&msg)==0);
    msg.message=WM_KEYUP;require(menu_input::dispatchMessage(&msg)==123);msg.message=WM_INPUT;require(menu_input::dispatchMessage(&msg)==123);msg.message=WM_SIZE;require(menu_input::dispatchMessage(&msg)==123);
    menu_input::setActive(false);msg.message=WM_MOUSEMOVE;require(menu_input::dispatchMessage(&msg)==123);
    GetRawInputData(nullptr,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));MSG empty{};DispatchMessageW(&empty);
    require(menu_input::initialize());POINT a{},b{};GetCursorPos(&a);menu_input::setActive(true);GetCursorPos(&b);require(b.x==menu_input::frozenCursor.x&&b.y==menu_input::frozenCursor.y);menu_input::shutdown();GetCursorPos(&b);
    std::cout<<"PASS: gameplay gating, menu input separation, releases, raw query, import install and restore\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
