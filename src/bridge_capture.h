#pragma once
#include <d3d12.h>
#include <cstdint>
struct BridgeBinding {
    UINT root,offset,reg,space; char kind;
    ID3D12Resource* resource; DXGI_FORMAT format;
    D3D12_RESOURCE_STATES state; bool stateKnown;
};
struct BridgePass {
    ID3D12GraphicsCommandList* list; uint64_t shader;
    ID3D12PipelineState* pipeline; ID3D12RootSignature* root;
    const BridgeBinding* bindings; UINT bindingCount;
    const unsigned char* camera; UINT cameraBytes;
    UINT constants[3],groups[3]; bool after;
    uint64_t actualShader=0; // Canonical role does not conceal the observed PSO hash.
    unsigned lightingSrvSpace=0;
    bool auxiliaryBuffer=false,auxiliaryViewKnown=false,auxiliaryCounter=false,extraOutput14=false;
    D3D12_UNORDERED_ACCESS_VIEW_DESC auxiliaryView{};
};
using BridgePassCallback=void(*)(const BridgePass*);
using BridgeQueueCallback=void(*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using BridgeRegister=void(*)(BridgePassCallback,BridgeQueueCallback);
// Copies descriptor DEFINITIONS into a private heap, never copies from a
// shader-visible source heap. Returns offsets/counts of native compute tables.
using BridgeCloneTables=bool(*)(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,UINT*,UINT*);
using BridgeRestore=void(*)(ID3D12GraphicsCommandList*,void(*)(void*),void*);
using BridgeBindClone=void(*)(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,const UINT*);
