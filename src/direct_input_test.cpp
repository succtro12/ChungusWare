#include "direct_input_gate.cpp"
#include <iostream>
HRESULT STDMETHODCALLTYPE fakeState(IUnknown*,DWORD size,void* data){memset(data,0x7f,size);return S_OK;}
HRESULT STDMETHODCALLTYPE fakeData(IUnknown*,DWORD,DIDEVICEOBJECTDATA* data,DWORD* count,DWORD){if(data&&*count)data[0].dwData=99;*count=1;return S_OK;}
int main(){
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);direct_input_gate::begin=reinterpret_cast<uintptr_t>(base);direct_input_gate::end=direct_input_gate::begin+nt->OptionalHeader.SizeOfImage;
    direct_input_gate::originals[0]=&fakeState;direct_input_gate::originalData[0]=&fakeData;DIMOUSESTATE2 mouse{};
    menu_input::setActive(false);direct_input_gate::state<0>(nullptr,sizeof(mouse),&mouse);if(!mouse.lX)return 1;
    menu_input::setActive(true);direct_input_gate::state<0>(nullptr,sizeof(mouse),&mouse);if(mouse.lX||mouse.lY||mouse.lZ||mouse.rgbButtons[0])return 2;
    unsigned char keys[256]{};direct_input_gate::state<0>(nullptr,sizeof(keys),keys);if(keys['W'])return 3;
    DIDEVICEOBJECTDATA event{};DWORD count=1;direct_input_gate::data<0>(nullptr,sizeof(event),&event,&count,0);if(count)return 4;
    menu_input::setActive(false);direct_input_gate::state<0>(nullptr,sizeof(mouse),&mouse);if(!mouse.lX)return 5;
    std::cout<<"PASS: DirectInput movement, mouse buttons, keyboard and buffered events blocked; normal input restored\n";return 0;
}
