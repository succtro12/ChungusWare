#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace chungus_ui {
inline std::filesystem::path localAsset(HMODULE module,const wchar_t* name){
    wchar_t path[32768]{};auto length=GetModuleFileNameW(module,path,32768);
    if(!length||length>=32768)throw std::runtime_error("module path unavailable");
    return std::filesystem::weakly_canonical(std::filesystem::path(path).parent_path())/L"ChungusWare"/L"assets"/name;
}
inline void assetNotice(const std::string& reason){OutputDebugStringA(("ChungusWare optional asset: "+reason+"\n").c_str());}
// Optional PNG is decoded into the reserved atlas region. Its absence never
// changes graphics feature initialization; the built-in neutral placeholder stays.
inline bool loadContainment(HMODULE module,std::vector<unsigned char>& atlas,std::string& status){
    try{
        auto path=localAsset(module,L"containment.png");
        if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>8*1024*1024){status="containment.png missing or exceeds 8 MiB; placeholder active";return false;}
        auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        struct ComScope{HRESULT hr;~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}} scope{com};
        if(FAILED(com)&&com!=RPC_E_CHANGED_MODE){status="PNG COM initialization unavailable; placeholder active";return false;}
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> converted;
        auto require=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("PNG decode rejected");};
        require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        require(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
        GUID container{};require(decoder->GetContainerFormat(&container));if(container!=GUID_ContainerFormatPng)throw std::runtime_error("mascot must be PNG");
        require(decoder->GetFrame(0,&frame));UINT width=0,height=0;require(frame->GetSize(&width,&height));
        if(!width||!height||width>2048||height>2048)throw std::runtime_error("PNG dimensions outside 1..2048");
        double fit=std::min(300.0/width,332.0/height);UINT outW=std::max(1u,UINT(std::lround(width*fit))),outH=std::max(1u,UINT(std::lround(height*fit)));
        require(factory->CreateBitmapScaler(&scaler));require(scaler->Initialize(frame.Get(),outW,outH,WICBitmapInterpolationModeFant));
        require(factory->CreateFormatConverter(&converted));require(converted->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        std::vector<unsigned char> pixels(size_t(outW)*outH*4);require(converted->CopyPixels(nullptr,outW*4,UINT(pixels.size()),pixels.data()));
        if(atlas.size()!=1024*1024*4)throw std::runtime_error("atlas size invalid");
        for(unsigned y=448;y<780;y++)std::fill_n(atlas.data()+(size_t(y)*1024)*4,300*4,0);
        unsigned x=(300-outW)/2,y=448+(332-outH)/2;
        for(unsigned row=0;row<outH;row++)std::copy_n(pixels.data()+size_t(row)*outW*4,outW*4,atlas.data()+((size_t(y+row)*1024)+x)*4);
        status="containment.png loaded module-locally";return true;
    }catch(const std::exception& e){status=std::string(e.what())+"; placeholder active";return false;}
}
}
