#include <windows.h>
#include "sr_quality.h"
namespace {
DWORD WINAPI worker(void*) {
    // Quality polling does not need another hook on NGX CreateFeature.
    // Initialization waits for the existing session's capability parameters.
    for (;;) { srQualityPoll(); Sleep(50); }
}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    }
    return TRUE;
}
