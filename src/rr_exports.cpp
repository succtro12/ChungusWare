#include "rr_backend.h"
extern "C" __declspec(dllexport) bedrock_rr::Backend* RrAllocate() { return new bedrock_rr::Backend(); }
extern "C" __declspec(dllexport) bool RrInitialize(bedrock_rr::Backend* b, ID3D12Device* d,
    const wchar_t* runtime, const wchar_t* log) { return b && runtime && log && b->initialize(d,runtime,log); }
extern "C" __declspec(dllexport) bool RrCreate(bedrock_rr::Backend* b, ID3D12GraphicsCommandList* l,
    unsigned iw,unsigned ih,unsigned ow,unsigned oh,bool hw,bool inverted) { return b && b->create(l,iw,ih,ow,oh,hw,inverted); }
extern "C" __declspec(dllexport) bool RrEvaluate(bedrock_rr::Backend* b, ID3D12GraphicsCommandList* l,
    const bedrock_rr::Frame* f) { return b && f && b->evaluate(l,*f); }
extern "C" __declspec(dllexport) const char* RrLastError(bedrock_rr::Backend* b) { return b ? b->error().c_str() : "Null RR context"; }
extern "C" __declspec(dllexport) void RrDestroyAfterGpuIdle(bedrock_rr::Backend* b) { if(b) { b->shutdownAfterGpuIdle(); delete b; } }
