#pragma once
#include <windows.h>
#include <atomic>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>

// Patch only the game's imports. ReShade keeps receiving the unfiltered input
// through its own imports, so the menu remains usable while gameplay is gated.
namespace menu_input {
inline std::atomic<bool> active{false};
inline std::atomic<unsigned> epoch{0};
inline POINT frozenCursor{};
inline decltype(&GetRawInputData) rawInput=nullptr;
inline decltype(&GetCursorPos) cursorPosition=nullptr;
inline decltype(&DispatchMessageW) dispatch=nullptr;
struct Patch {void** slot;void* original;void* replacement;};
inline std::vector<Patch> patches;
inline std::atomic<unsigned> rawReads{0},mouseReads{0},cursorReads{0},blockedMessages{0};
inline std::atomic<unsigned> totalRawReads{0};

inline void filterRaw(RAWINPUT* data,UINT bytes){
    if(!data||bytes<sizeof(RAWINPUTHEADER))return;
    if(data->header.dwType==RIM_TYPEMOUSE&&bytes>=sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)){
        data->data.mouse.lLastX=data->data.mouse.lLastY=0;
        data->data.mouse.usButtonFlags &= RI_MOUSE_LEFT_BUTTON_UP|RI_MOUSE_RIGHT_BUTTON_UP|RI_MOUSE_MIDDLE_BUTTON_UP|RI_MOUSE_BUTTON_4_UP|RI_MOUSE_BUTTON_5_UP;
        data->data.mouse.usButtonData=0;
    }else if(data->header.dwType==RIM_TYPEKEYBOARD&&bytes>=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)){
        // Keep real releases so keys held before opening cannot get stuck.
        if(!(data->data.keyboard.Flags&RI_KEY_BREAK)){
            data->data.keyboard.Flags|=RI_KEY_BREAK;
            data->data.keyboard.VKey=0;
            data->data.keyboard.MakeCode=0;
            data->data.keyboard.Message=WM_KEYUP;
        }
    }
}
inline UINT WINAPI readRaw(HRAWINPUT handle,UINT command,LPVOID data,PUINT size,UINT headerSize){
    totalRawReads.fetch_add(1,std::memory_order_relaxed);
    auto result=rawInput(handle,command,data,size,headerSize);
    if(active.load(std::memory_order_acquire)&&command==RID_INPUT&&data&&result!=UINT(-1)){rawReads++;if(static_cast<RAWINPUT*>(data)->header.dwType==RIM_TYPEMOUSE)mouseReads++;filterRaw(static_cast<RAWINPUT*>(data),result);}
    return result;
}
inline BOOL WINAPI readCursor(LPPOINT point){
    auto result=cursorPosition(point);
    if(result&&point&&active.load(std::memory_order_acquire)){cursorReads++;*point=frozenCursor;}
    return result;
}
inline bool blockedMessage(UINT m){
    return m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_CHAR||m==WM_SYSCHAR||m==WM_UNICHAR||
        m==WM_MOUSEMOVE||m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL||
        m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK||m==WM_RBUTTONDOWN||m==WM_RBUTTONDBLCLK||
        m==WM_MBUTTONDOWN||m==WM_MBUTTONDBLCLK||m==WM_XBUTTONDOWN||m==WM_XBUTTONDBLCLK;
}
inline LRESULT WINAPI dispatchMessage(const MSG* message){
    if(message&&active.load(std::memory_order_acquire)&&blockedMessage(message->message)){blockedMessages++;return 0;}
    // WM_INPUT must be dispatched for foreground raw-input cleanup. Its data
    // is filtered in readRaw; release and non-input messages stay untouched.
    return dispatch(message);
}
inline void setActive(bool value){
    bool before=active.load(std::memory_order_relaxed);
    if(value&&!before)GetCursorPos(&frozenCursor);
    active.store(value,std::memory_order_release);
    if(before!=value)epoch.fetch_add(1,std::memory_order_release);
    if(before!=value){wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);auto directory=std::filesystem::path(exe).parent_path()/L"fg";std::filesystem::create_directories(directory);std::ofstream log(directory/L"menu-input.jsonl",std::ios::app);log<<"{\"open\":"<<(value?"true":"false")<<",\"import_hooks\":"<<patches.size()<<",\"total_raw_reads\":"<<totalRawReads.load()<<",\"raw_reads\":"<<rawReads.load()<<",\"mouse_reads\":"<<mouseReads.load()<<",\"cursor_reads\":"<<cursorReads.load()<<",\"blocked_messages\":"<<blockedMessages.load()<<"}\n";}
}
inline bool initialize(){
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress)return false;
    for(auto descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);descriptor->Name;descriptor++){
        if(_stricmp(reinterpret_cast<const char*>(base+descriptor->Name),"user32.dll")||!descriptor->OriginalFirstThunk)continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->OriginalFirstThunk);
        auto table=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->FirstThunk);
        for(;names->u1.AddressOfData;names++,table++){
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData)->Name;
            void* replacement=nullptr;
            if(!strcmp(reinterpret_cast<const char*>(name),"GetRawInputData")){rawInput=reinterpret_cast<decltype(rawInput)>(table->u1.Function);replacement=reinterpret_cast<void*>(&readRaw);}
            if(!strcmp(reinterpret_cast<const char*>(name),"GetCursorPos")){cursorPosition=reinterpret_cast<decltype(cursorPosition)>(table->u1.Function);replacement=reinterpret_cast<void*>(&readCursor);}
            if(!strcmp(reinterpret_cast<const char*>(name),"DispatchMessageW")){dispatch=reinterpret_cast<decltype(dispatch)>(table->u1.Function);replacement=reinterpret_cast<void*>(&dispatchMessage);}
            if(!replacement)continue;
            auto slot=reinterpret_cast<void**>(&table->u1.Function);DWORD old=0;
            if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){
                patches.push_back({slot,*slot,replacement});InterlockedExchangePointer(slot,replacement);VirtualProtect(slot,sizeof(void*),old,&old);
            }
        }
    }
    return patches.size()==3;
}
inline void shutdown(){
    setActive(false);
    for(auto& p:patches){DWORD old=0;if(VirtualProtect(p.slot,sizeof(void*),PAGE_READWRITE,&old)){InterlockedCompareExchangePointer(p.slot,p.original,p.replacement);VirtualProtect(p.slot,sizeof(void*),old,&old);}}
    patches.clear();
}
}
