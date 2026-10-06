#define DIRECTINPUT_VERSION 0x0800
#include "menu_input.h"
#include <dinput.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <intrin.h>
#include <mutex>
#ifdef DI_GATE_PROBE
#include <imgui.h>
#include <reshade.hpp>
#endif
namespace direct_input_gate {
using State=HRESULT(STDMETHODCALLTYPE*)(IUnknown*,DWORD,void*);
using Data=HRESULT(STDMETHODCALLTYPE*)(IUnknown*,DWORD,DIDEVICEOBJECTDATA*,DWORD*,DWORD);
State originals[2]{};Data originalData[2]{};bool ready=false;
uintptr_t begin=0,end=0;std::atomic<unsigned> observed{0},blocked{0};std::mutex mutex;std::ofstream log;
bool gameCaller(void* p){auto a=reinterpret_cast<uintptr_t>(p);return a>=begin&&a<end;}
void record(const char* method,DWORD bytes,void* caller,HRESULT result,bool capturing){
    auto sample=observed.fetch_add(1);if(sample<16||capturing&&sample%120==0){std::lock_guard guard(mutex);log<<"{\"method\":\""<<method<<"\",\"bytes\":"<<bytes<<",\"caller_rva\":"<<(reinterpret_cast<uintptr_t>(caller)-begin)<<",\"result\":"<<unsigned(result)<<",\"captured\":"<<(capturing?"true":"false")<<",\"blocked\":"<<blocked.load()<<"}\n";log.flush();}
}
template<unsigned I> HRESULT STDMETHODCALLTYPE state(IUnknown* device,DWORD bytes,void* buffer){
    auto caller=_ReturnAddress();auto result=originals[I](device,bytes,buffer);
    if(!gameCaller(caller))return result;
    bool capturing=menu_input::active.load(std::memory_order_acquire);
    if(capturing&&SUCCEEDED(result)&&buffer&&(bytes==sizeof(DIMOUSESTATE)||bytes==sizeof(DIMOUSESTATE2)||bytes==256)){memset(buffer,0,bytes);blocked++;}
    record("GetDeviceState",bytes,caller,result,capturing);return result;
}
template<unsigned I> HRESULT STDMETHODCALLTYPE data(IUnknown* device,DWORD bytes,DIDEVICEOBJECTDATA* buffer,DWORD* count,DWORD flags){
    auto caller=_ReturnAddress();auto result=originalData[I](device,bytes,buffer,count,flags);
    if(!gameCaller(caller))return result;bool capturing=menu_input::active.load(std::memory_order_acquire);
    if(capturing&&SUCCEEDED(result)&&count){*count=0;result=DI_OK;blocked++;}
    record("GetDeviceData",bytes,caller,result,capturing);return result;
}
bool initialize(){
    if(ready)return true;auto dll=GetModuleHandleW(L"dinput8.dll");if(!dll)return false;
    auto create=reinterpret_cast<HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*)>(GetProcAddress(dll,"DirectInput8Create"));if(!create)return false;
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);begin=reinterpret_cast<uintptr_t>(base);end=begin+nt->OptionalHeader.SizeOfImage;
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto dir=std::filesystem::path(exe).parent_path()/L"fg";std::filesystem::create_directories(dir);log.open(dir/L"menu-directinput.jsonl",std::ios::app);MH_Initialize();
    Microsoft::WRL::ComPtr<IDirectInput8W> input;Microsoft::WRL::ComPtr<IDirectInputDevice8W> mouse;
    if(FAILED(create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(input.GetAddressOf()),nullptr))||FAILED(input->CreateDevice(GUID_SysMouse,&mouse,nullptr)))return false;
    auto table=*reinterpret_cast<void***>(mouse.Get());auto a=MH_CreateHook(table[9],&state<0>,reinterpret_cast<void**>(&originals[0]));auto b=MH_CreateHook(table[10],&data<0>,reinterpret_cast<void**>(&originalData[0]));if(a==MH_OK)MH_EnableHook(table[9]);if(b==MH_OK)MH_EnableHook(table[10]);
    Microsoft::WRL::ComPtr<IDirectInput8A> ansi;Microsoft::WRL::ComPtr<IDirectInputDevice8A> ansiMouse;
    if(SUCCEEDED(create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8A,reinterpret_cast<void**>(ansi.GetAddressOf()),nullptr))&&SUCCEEDED(ansi->CreateDevice(GUID_SysMouse,&ansiMouse,nullptr))){auto at=*reinterpret_cast<void***>(ansiMouse.Get());if(at[9]!=table[9]&&MH_CreateHook(at[9],&state<1>,reinterpret_cast<void**>(&originals[1]))==MH_OK)MH_EnableHook(at[9]);if(at[10]!=table[10]&&MH_CreateHook(at[10],&data<1>,reinterpret_cast<void**>(&originalData[1]))==MH_OK)MH_EnableHook(at[10]);}
    log<<"{\"state_hook\":"<<unsigned(a)<<",\"data_hook\":"<<unsigned(b)<<"}\n";log.flush();ready=a==MH_OK&&b==MH_OK;return ready;
}
}
bool rrDirectInputInitialize(){return direct_input_gate::initialize();}
unsigned rrDirectInputBlocked(){return direct_input_gate::blocked.load();}
#ifdef DI_GATE_PROBE
void probeOverlay(reshade::api::effect_runtime*){auto& io=ImGui::GetIO();menu_input::setActive(io.WantCaptureMouse||io.WantCaptureKeyboard);}
DWORD WINAPI startProbe(void* p){auto module=static_cast<HMODULE>(p);if(!reshade::register_addon(module,GetModuleHandleW(L"dxgi.dll")))return 1;rrDirectInputInitialize();reshade::register_event<reshade::addon_event::reshade_overlay>(&probeOverlay);return 0;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);auto thread=CreateThread(nullptr,0,startProbe,h,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
#endif
