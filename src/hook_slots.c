// Derive COM method slots from the installed Windows SDK. Never hand-count
// inherited interfaces: an incorrect slot has a different calling signature.
#define CINTERFACE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include "hook_slots.h"
#define SLOT(I,M) (offsetof(I##Vtbl,M)/sizeof(void*))
const HookSlots* BedrockRrHookSlots(void) {
    static const HookSlots slots={
        SLOT(ID3D12Device,CreateComputePipelineState),SLOT(ID3D12Device,CreateRootSignature),
        SLOT(ID3D12Device,CreateConstantBufferView),SLOT(ID3D12Device,CreateShaderResourceView),SLOT(ID3D12Device,CreateUnorderedAccessView),
        SLOT(ID3D12Device,CopyDescriptors),SLOT(ID3D12Device,CopyDescriptorsSimple),
        SLOT(ID3D12GraphicsCommandList,Reset),SLOT(ID3D12GraphicsCommandList,Dispatch),SLOT(ID3D12GraphicsCommandList,SetPipelineState),
        SLOT(ID3D12GraphicsCommandList,SetDescriptorHeaps),SLOT(ID3D12GraphicsCommandList,SetComputeRootSignature),SLOT(ID3D12GraphicsCommandList,SetComputeRootDescriptorTable),
        SLOT(ID3D12GraphicsCommandList,SetComputeRoot32BitConstant),SLOT(ID3D12GraphicsCommandList,SetComputeRoot32BitConstants),SLOT(ID3D12GraphicsCommandList,SetComputeRootConstantBufferView),
        SLOT(ID3D12Device2,CreatePipelineState),SLOT(ID3D12Device5,CreateStateObject),
        SLOT(ID3D12GraphicsCommandList4,SetPipelineState1),SLOT(ID3D12GraphicsCommandList4,DispatchRays),
        SLOT(ID3D12Resource,Map),SLOT(ID3D12Resource,Unmap),
        SLOT(ID3D12GraphicsCommandList,ResourceBarrier),SLOT(ID3D12CommandQueue,ExecuteCommandLists),
        SLOT(ID3D12Device,CreateCommittedResource),SLOT(ID3D12Device,CreatePlacedResource),
        SLOT(ID3D12Device4,CreateCommittedResource1),SLOT(ID3D12Device8,CreateCommittedResource2),SLOT(ID3D12Device8,CreatePlacedResource1)
    };
    return &slots;
}
