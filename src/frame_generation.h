#pragma once
#include <d3d12.h>
struct FgState {unsigned requested,maximum,evaluations,presented;unsigned result;const char* status;float renderedFps=0,presentedFps=0;unsigned cadence=0;float refreshHz=0;unsigned pacingMode=2;float targetFps=0;bool managed=false;unsigned skipped=0;};
FgState fgState();
void fgSelect(unsigned generated);
void fgCadence(unsigned refreshDivisor);
void fgPacing(unsigned mode,float targetPresentedFps);
void fgPause(bool menuOpen);
bool fgRecording();
void fgInitialize();
void fgShutdown();
void fgCapture(ID3D12GraphicsCommandList*,ID3D12Resource* linearDepth,ID3D12Resource* motion,const unsigned char* camera,unsigned width,unsigned height,unsigned frame);
void fgSubmitted(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
struct FgDeliveryState {bool available=false,labelsComplete=false;float generatedFps=0,presentedFps=0,meanMs=0,p99Ms=0,worstMs=0,realToGeneratedMs=0,generatedToRealMs=0,ageSeconds=0,displayLatencyMs=0;unsigned droppedGenerated=0,unknownFrames=0,unmatched=0,rows=0;int syncInterval=-1;const char* status="ETW telemetry not started";};
FgDeliveryState fgDeliveryState();
