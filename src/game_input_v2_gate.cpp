#include "menu_input.h"
#include <GameInput.h>
using namespace GameInput::v2;
#include <MinHook.h>
#include <wrl/client.h>
#include <intrin.h>
#include <mutex>
#include <unordered_map>
namespace game_input_v2_gate {
using Mouse=bool(STDMETHODCALLTYPE*)(IGameInputReading*,GameInputMouseState*);
using Keys=uint32_t(STDMETHODCALLTYPE*)(IGameInputReading*,uint32_t,GameInputKeyState*);
Mouse originalMouse=nullptr;Keys originalKeys=nullptr;
IGameInput* discoveryClient=nullptr;
uintptr_t gameBegin=0,gameEnd=0;std::mutex mutex;
struct Position {
    int64_t raw[6]{},delivered[6]{};bool initialized=false;unsigned epoch=0;
    Microsoft::WRL::ComPtr<IGameInputDevice> device;
};
std::unordered_map<IGameInputDevice*,Position> positions;
std::atomic<unsigned> reads{0},blocked{0},allReads{0};
bool gameCaller(void* address){auto p=reinterpret_cast<uintptr_t>(address);return p>=gameBegin&&p<gameEnd;}
void filter(Position& position,GameInputMouseState* state,bool captured,unsigned epoch){
    int64_t values[]={state->positionX,state->positionY,state->absolutePositionX,state->absolutePositionY,state->wheelX,state->wheelY};
    if(!position.initialized){position.initialized=true;position.epoch=epoch;for(unsigned i=0;i<6;i++)position.raw[i]=position.delivered[i]=values[i];}
    bool transition=position.epoch!=epoch;
    for(unsigned i=0;i<6;i++){if(!captured&&!transition)position.delivered[i]+=values[i]-position.raw[i];position.raw[i]=values[i];}
    position.epoch=epoch;
    state->positionX=position.delivered[0];state->positionY=position.delivered[1];state->absolutePositionX=position.delivered[2];state->absolutePositionY=position.delivered[3];state->wheelX=position.delivered[4];state->wheelY=position.delivered[5];
    if(captured){state->buttons=GameInputMouseNone;blocked++;}
}
bool STDMETHODCALLTYPE mouse(IGameInputReading* reading,GameInputMouseState* state){
    auto caller=_ReturnAddress();bool fromGame=gameCaller(caller);auto result=originalMouse(reading,state);
    if(allReads.fetch_add(1)<8){
        HMODULE owner=nullptr;wchar_t path[MAX_PATH]{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(caller),&owner);GetModuleFileNameW(owner,path,MAX_PATH);
        wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
        std::ofstream log(std::filesystem::path(exe).parent_path()/L"fg"/L"menu-gameinput-v2-calls.jsonl",std::ios::app);
        log<<"{\"owner\":\""<<std::filesystem::path(path).filename().string()<<"\",\"caller_offset\":"<<(uintptr_t(caller)-uintptr_t(owner))<<",\"from_game\":"<<(fromGame?"true":"false")<<",\"success\":"<<(result?"true":"false")<<"}\n";
    }
    if(!result||!state||!fromGame)return result;
    reads++;Microsoft::WRL::ComPtr<IGameInputDevice> device;reading->GetDevice(&device);
    std::lock_guard guard(mutex);auto& position=positions[device.Get()];position.device=device;
    filter(position,state,menu_input::active.load(std::memory_order_acquire),menu_input::epoch.load(std::memory_order_acquire));
    return result;
}
uint32_t STDMETHODCALLTYPE keys(IGameInputReading* reading,uint32_t count,GameInputKeyState* state){
    bool fromGame=gameCaller(_ReturnAddress());auto result=originalKeys(reading,count,state);
    if(fromGame&&menu_input::active.load(std::memory_order_acquire)){if(state)memset(state,0,sizeof(*state)*count);return 0;}return result;
}
bool initialize(){
    if(originalMouse)return true;
    auto module=GetModuleHandleW(L"GameInputRedist.dll");if(!module)module=GetModuleHandleW(L"GameInput.dll");if(!module)return false;
    auto create=reinterpret_cast<HRESULT(WINAPI*)(IUnknown**)>(GetProcAddress(module,"GameInputCreate"));if(!create)return false;
    // Enumeration is asynchronous. Keep this discovery client alive and retry
    // on a later overlay frame; never wait for a device on the render thread.
    if(!discoveryClient){Microsoft::WRL::ComPtr<IUnknown> legacy;if(FAILED(create(&legacy))||FAILED(legacy->QueryInterface(__uuidof(IGameInput),reinterpret_cast<void**>(&discoveryClient))))return false;}
    Microsoft::WRL::ComPtr<IGameInputReading> reading;if(FAILED(discoveryClient->GetCurrentReading(GameInputKindMouse,nullptr,&reading)))return false;
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);gameBegin=reinterpret_cast<uintptr_t>(base);gameEnd=gameBegin+nt->OptionalHeader.SizeOfImage;
    auto table=*reinterpret_cast<void***>(reading.Get());MH_Initialize();
    auto a=MH_CreateHook(table[14],&mouse,reinterpret_cast<void**>(&originalMouse));auto b=MH_CreateHook(table[13],&keys,reinterpret_cast<void**>(&originalKeys));
    if(a==MH_OK)MH_EnableHook(table[14]);if(b==MH_OK)MH_EnableHook(table[13]);
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);auto path=std::filesystem::path(exe).parent_path()/L"fg"/L"menu-input.jsonl";
    std::ofstream log(path,std::ios::app);log<<"{\"gameinput_v2_mouse_hook\":"<<unsigned(a)<<",\"gameinput_v2_keys_hook\":"<<unsigned(b)<<"}\n";
    return a==MH_OK&&b==MH_OK;
}
}
bool rrGameInputV2Initialize(){return game_input_v2_gate::initialize();}
unsigned rrGameInputV2Blocked(){return game_input_v2_gate::blocked.load();}
unsigned rrGameInputV2Reads(){return game_input_v2_gate::reads.load();}
unsigned rrGameInputV2AllReads(){return game_input_v2_gate::allReads.load();}

