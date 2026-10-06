// Factory bridge: never expose a Streamline factory to outer ReShade/addon hooks.
// Its CreateSwapChain callbacks forward through the saved native trampoline.
namespace managed_fg {
std::vector<ComPtr<IDXGIFactory7>> managedFactories;
using FactoryCreateSwapChain=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain);
FactoryCreateSwapChain originalFactorySwap=nullptr;
using FactoryCreateSwapChainForHwnd=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain);
FactoryCreateSwapChainForHwnd originalFactoryHwnd=nullptr;
using FactoryCreateSwapChainForCoreWindow=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain);
FactoryCreateSwapChainForCoreWindow originalFactoryCore=nullptr;
using FactoryCreateSwapChainForComposition=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain);
FactoryCreateSwapChainForComposition originalFactoryComposition=nullptr;
struct NativeFactoryBridge final:IDXGIFactory7 { ComPtr<IDXGIFactory7> native;LONG refs=1;explicit NativeFactoryBridge(IDXGIFactory7* p):native(p){}
HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(iid==__uuidof(IUnknown)||iid==__uuidof(IDXGIObject)||iid==__uuidof(IDXGIFactory)||iid==__uuidof(IDXGIFactory1)||iid==__uuidof(IDXGIFactory2)||iid==__uuidof(IDXGIFactory3)||iid==__uuidof(IDXGIFactory4)||iid==__uuidof(IDXGIFactory5)||iid==__uuidof(IDXGIFactory6)||iid==__uuidof(IDXGIFactory7)){*out=static_cast<IDXGIFactory7*>(this);AddRef();return S_OK;}return native->QueryInterface(iid,out);}
ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
ULONG STDMETHODCALLTYPE Release() override {auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override {return native->SetPrivateData(Name,DataSize,pData);}
HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override {return native->SetPrivateDataInterface(Name,pUnknown);}
HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override {return native->GetPrivateData(Name,pDataSize,pData);}
HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override {return native->GetParent(riid,ppParent);}
HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter) override {return native->EnumAdapters(Adapter,ppAdapter);}
HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override {return native->MakeWindowAssociation(WindowHandle,Flags);}
HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *pWindowHandle) override {return native->GetWindowAssociation(pWindowHandle);}
HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) override {auto r=originalFactorySwap(native.Get(),pDevice,pDesc,ppSwapChain);if(SUCCEEDED(r)&&ppSwapChain)fg_delivery::swapchain(*ppSwapChain);return r;}
HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) override {return native->CreateSoftwareAdapter(Module,ppAdapter);}
HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter) override {return native->EnumAdapters1(Adapter,ppAdapter);}
BOOL STDMETHODCALLTYPE IsCurrent() override {return native->IsCurrent();}
BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override {return native->IsWindowedStereoEnabled();}
HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override {auto r=originalFactoryHwnd(native.Get(),pDevice,hWnd,pDesc,pFullscreenDesc,pRestrictToOutput,ppSwapChain);if(SUCCEEDED(r)&&ppSwapChain)fg_delivery::swapchain(*ppSwapChain);return r;}
HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override {auto r=originalFactoryCore(native.Get(),pDevice,pWindow,pDesc,pRestrictToOutput,ppSwapChain);if(SUCCEEDED(r)&&ppSwapChain)fg_delivery::swapchain(*ppSwapChain);return r;}
HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid) override {return native->GetSharedResourceAdapterLuid(hResource,pLuid);}
HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override {return native->RegisterStereoStatusWindow(WindowHandle,wMsg,pdwCookie);}
HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie) override {return native->RegisterStereoStatusEvent(hEvent,pdwCookie);}
void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) override {native->UnregisterStereoStatus(dwCookie);}
HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override {return native->RegisterOcclusionStatusWindow(WindowHandle,wMsg,pdwCookie);}
HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) override {return native->RegisterOcclusionStatusEvent(hEvent,pdwCookie);}
void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) override {native->UnregisterOcclusionStatus(dwCookie);}
HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override {auto r=originalFactoryComposition(native.Get(),pDevice,pDesc,pRestrictToOutput,ppSwapChain);if(SUCCEEDED(r)&&ppSwapChain)fg_delivery::swapchain(*ppSwapChain);return r;}
UINT STDMETHODCALLTYPE GetCreationFlags() override {return native->GetCreationFlags();}
HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void **ppvAdapter) override {return native->EnumAdapterByLuid(AdapterLuid,riid,ppvAdapter);}
HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void **ppvAdapter) override {return native->EnumWarpAdapter(riid,ppvAdapter);}
HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override {return native->CheckFeatureSupport(Feature,pFeatureSupportData,FeatureSupportDataSize);}
HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) override {return native->EnumAdapterByGpuPreference(Adapter,GpuPreference,riid,ppvAdapter);}
HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD *pdwCookie) override {return native->RegisterAdaptersChangedEvent(hEvent,pdwCookie);}
HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD dwCookie) override {return native->UnregisterAdaptersChangedEvent(dwCookie);}
};
HRESULT STDMETHODCALLTYPE managedCreateSwapChain(IDXGIFactory2* factory,IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) {
if(internal||!ready)return originalFactorySwap(factory,pDevice,pDesc,ppSwapChain);
Internal scope;ComPtr<IDXGIFactory7> native;if(FAILED(factory->QueryInterface(IID_PPV_ARGS(&native))))return originalFactorySwap(factory,pDevice,pDesc,ppSwapChain);
IDXGIFactory7* bridged=new NativeFactoryBridge(native.Get());auto result=upgrade(reinterpret_cast<void**>(&bridged));if(result!=sl::Result::eOk){bridged->Release();return originalFactorySwap(factory,pDevice,pDesc,ppSwapChain);}
auto hr=bridged->CreateSwapChain(pDevice,pDesc,ppSwapChain);managedFactories.emplace_back();managedFactories.back().Attach(bridged);report("Managed factory CreateSwapChain",unsigned(hr));return hr;
}
HRESULT STDMETHODCALLTYPE managedCreateSwapChainForHwnd(IDXGIFactory2* factory,IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
if(internal||!ready)return originalFactoryHwnd(factory,pDevice,hWnd,pDesc,pFullscreenDesc,pRestrictToOutput,ppSwapChain);
Internal scope;ComPtr<IDXGIFactory7> native;if(FAILED(factory->QueryInterface(IID_PPV_ARGS(&native))))return originalFactoryHwnd(factory,pDevice,hWnd,pDesc,pFullscreenDesc,pRestrictToOutput,ppSwapChain);
IDXGIFactory7* bridged=new NativeFactoryBridge(native.Get());auto result=upgrade(reinterpret_cast<void**>(&bridged));if(result!=sl::Result::eOk){bridged->Release();return originalFactoryHwnd(factory,pDevice,hWnd,pDesc,pFullscreenDesc,pRestrictToOutput,ppSwapChain);}
auto hr=bridged->CreateSwapChainForHwnd(pDevice,hWnd,pDesc,pFullscreenDesc,pRestrictToOutput,ppSwapChain);managedFactories.emplace_back();managedFactories.back().Attach(bridged);report("Managed factory CreateSwapChainForHwnd",unsigned(hr));return hr;
}
HRESULT STDMETHODCALLTYPE managedCreateSwapChainForCoreWindow(IDXGIFactory2* factory,IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
if(internal||!ready)return originalFactoryCore(factory,pDevice,pWindow,pDesc,pRestrictToOutput,ppSwapChain);
Internal scope;ComPtr<IDXGIFactory7> native;if(FAILED(factory->QueryInterface(IID_PPV_ARGS(&native))))return originalFactoryCore(factory,pDevice,pWindow,pDesc,pRestrictToOutput,ppSwapChain);
IDXGIFactory7* bridged=new NativeFactoryBridge(native.Get());auto result=upgrade(reinterpret_cast<void**>(&bridged));if(result!=sl::Result::eOk){bridged->Release();return originalFactoryCore(factory,pDevice,pWindow,pDesc,pRestrictToOutput,ppSwapChain);}
auto hr=bridged->CreateSwapChainForCoreWindow(pDevice,pWindow,pDesc,pRestrictToOutput,ppSwapChain);managedFactories.emplace_back();managedFactories.back().Attach(bridged);report("Managed factory CreateSwapChainForCoreWindow",unsigned(hr));return hr;
}
HRESULT STDMETHODCALLTYPE managedCreateSwapChainForComposition(IDXGIFactory2* factory,IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
if(internal||!ready)return originalFactoryComposition(factory,pDevice,pDesc,pRestrictToOutput,ppSwapChain);
Internal scope;ComPtr<IDXGIFactory7> native;if(FAILED(factory->QueryInterface(IID_PPV_ARGS(&native))))return originalFactoryComposition(factory,pDevice,pDesc,pRestrictToOutput,ppSwapChain);
IDXGIFactory7* bridged=new NativeFactoryBridge(native.Get());auto result=upgrade(reinterpret_cast<void**>(&bridged));if(result!=sl::Result::eOk){bridged->Release();return originalFactoryComposition(factory,pDevice,pDesc,pRestrictToOutput,ppSwapChain);}
auto hr=bridged->CreateSwapChainForComposition(pDevice,pDesc,pRestrictToOutput,ppSwapChain);managedFactories.emplace_back();managedFactories.back().Attach(bridged);report("Managed factory CreateSwapChainForComposition",unsigned(hr));return hr;
}
bool hookNativeFactory(IUnknown* factory){ComPtr<IDXGIFactory2> native;constexpr GUID unwrap={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};ComPtr<IUnknown> unwrapped;if(SUCCEEDED(factory->QueryInterface(unwrap,reinterpret_cast<void**>(unwrapped.GetAddressOf()))))unwrapped.As(&native);else factory->QueryInterface(IID_PPV_ARGS(&native));if(!native)return false;auto table=*reinterpret_cast<void***>(native.Get());
if(!originalFactorySwap){auto h=MH_CreateHook(table[10],&managedCreateSwapChain,reinterpret_cast<void**>(&originalFactorySwap));report("Hook native factory CreateSwapChain",unsigned(h));if(h!=MH_OK)return false;MH_EnableHook(table[10]);}
if(!originalFactoryHwnd){auto h=MH_CreateHook(table[15],&managedCreateSwapChainForHwnd,reinterpret_cast<void**>(&originalFactoryHwnd));report("Hook native factory CreateSwapChainForHwnd",unsigned(h));if(h!=MH_OK)return false;MH_EnableHook(table[15]);}
if(!originalFactoryCore){auto h=MH_CreateHook(table[16],&managedCreateSwapChainForCoreWindow,reinterpret_cast<void**>(&originalFactoryCore));report("Hook native factory CreateSwapChainForCoreWindow",unsigned(h));if(h!=MH_OK)return false;MH_EnableHook(table[16]);}
if(!originalFactoryComposition){auto h=MH_CreateHook(table[24],&managedCreateSwapChainForComposition,reinterpret_cast<void**>(&originalFactoryComposition));report("Hook native factory CreateSwapChainForComposition",unsigned(h));if(h!=MH_OK)return false;MH_EnableHook(table[24]);}
return true;}
}
