#define FG_DIAGNOSTIC
#define fgState fgDiagnosticState
#define fgSelect fgDiagnosticSelect
#define fgPause fgDiagnosticPause
#define fgRecording fgDiagnosticRecording
#define fgCapture fgDiagnosticCapture
#define fgSubmitted fgDiagnosticSubmitted
#define fgInitialize fgDiagnosticInitialize
#define fgShutdown fgDiagnosticShutdown
#define fgCadence fgDiagnosticCadence
#include "frame_generation.cpp"
HRESULT fgDiagnosticPresent(IDXGISwapChain3* s,UINT sync,UINT flags,HRESULT(*call)(void*),void* context){auto before=fg::presented;auto hr=fg::present(s,flags,[&](UINT unused=0,bool realFrame=true){return fg::tracePresent(s,sync,flags,realFrame,[&]{return call(context);});});if(!(flags&DXGI_PRESENT_TEST)){fg::rates(before);fg::sleep();}return hr;}
