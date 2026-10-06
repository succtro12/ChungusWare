#include "chungus_ui_renderer.h"
#include "local_ui_assets.h"
#include "system_ui_font.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace chungus_ui {
using namespace reshade::api;using Microsoft::WRL::ComPtr;
static const char* shader=R"(
cbuffer QuadData:register(b0){float4 rect;float4 uvRect;float4 topColor;float4 bottomColor;};
Texture2D atlas:register(t0);SamplerState pixels:register(s0);
struct Vertex{float4 pos:SV_Position;float2 uv:TEXCOORD0;float4 color:COLOR0;};
Vertex vs(uint id:SV_VertexID){float2 corners[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};float2 p=corners[id];Vertex v;v.pos=float4(rect.xy+p*rect.zw,0,1);v.uv=uvRect.xy+p*uvRect.zw;v.color=lerp(topColor,bottomColor,p.y);return v;}
float4 ps(Vertex v):SV_Target{return atlas.Sample(pixels,v.uv)*v.color;}
)";
void Renderer::release(){if(device){for(auto& [resource,view]:targets)device->destroy_resource_view(view);if(pipeline.handle)device->destroy_pipeline(pipeline);if(atlasView.handle)device->destroy_resource_view(atlasView);if(atlas.handle)device->destroy_resource(atlas);if(sampler.handle)device->destroy_sampler(sampler);if(layout.handle)device->destroy_pipeline_layout(layout);}targets.clear();pipeline={};atlasView={};atlas={};sampler={};layout={};device=nullptr;targetFormat=format::unknown;}
bool Renderer::present(effect_runtime* runtime,const std::vector<Quad>& quads,HMODULE module){
    auto d=runtime->get_device();auto buffer=runtime->get_current_back_buffer();auto f=d->get_resource_desc(buffer).texture.format;
    if(device!=d||targetFormat!=f){release();if(!initialize(d,f,module))return false;}
    auto& target=targets[buffer.handle];if(!target.handle&&!d->create_resource_view(buffer,resource_usage::render_target,resource_view_desc(f),&target)){error="ui final target view failed";return false;}
    auto list=runtime->get_command_queue()->get_immediate_command_list();list->barrier(buffer,resource_usage::present,resource_usage::render_target);
    const bool result=draw(runtime,list,target,quads,module);list->barrier(buffer,resource_usage::render_target,resource_usage::present);return result;
}
bool Renderer::initialize(reshade::api::device* d,format f,HMODULE module){
    device=d;targetFormat=f;error.clear();auto fail=[&](const char* what){error=what;release();return false;};
    HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(102),RT_RCDATA);if(!resource||SizeofResource(module,resource)!=1024*1024*4)return fail("ui atlas resource missing");
    auto embedded=static_cast<const unsigned char*>(LockResource(LoadResource(module,resource)));if(!embedded)return fail("ui atlas unavailable");
    std::vector<unsigned char> pixels(embedded,embedded+1024*1024*4);applyLocalUiFont(pixels);std::string assetStatus;loadContainment(module,pixels,assetStatus);assetNotice(assetStatus);
    subresource_data initial{pixels.data(),1024*4,1024*1024*4};
    resource_desc desc(1024,1024,1,1,format::r8g8b8a8_unorm,1,memory_heap::gpu_only,resource_usage::shader_resource);
    if(!device->create_resource(desc,&initial,resource_usage::shader_resource,&atlas))return fail("ui atlas allocation failed");
    if(!device->create_resource_view(atlas,resource_usage::shader_resource,resource_view_desc(format::r8g8b8a8_unorm),&atlasView))return fail("ui atlas view failed");
    sampler_desc sampling{};sampling.filter=filter_mode::min_mag_mip_point;sampling.address_u=sampling.address_v=sampling.address_w=texture_address_mode::clamp;
    if(!device->create_sampler(sampling,&sampler))return fail("ui sampler failed");
    constant_range constants{};constants.count=16;constants.visibility=shader_stage::all_graphics;
    descriptor_range image{};image.count=1;image.type=descriptor_type::shader_resource_view;image.visibility=shader_stage::pixel;
    descriptor_range sample{};sample.count=1;sample.type=descriptor_type::sampler;sample.visibility=shader_stage::pixel;
    pipeline_layout_param params[]={pipeline_layout_param(constants),pipeline_layout_param(image),pipeline_layout_param(sample)};
    if(!device->create_pipeline_layout(3,params,&layout))return fail("ui root signature failed");
    ComPtr<ID3DBlob> vs,ps,messages;auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& code){return SUCCEEDED(D3DCompile(shader,strlen(shader),"ChungusWare owned UI",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&messages));};
    if(!compile("vs","vs_5_0",vs)||!compile("ps","ps_5_0",ps))return fail("ui shader compilation failed");
    shader_desc vertex{vs->GetBufferPointer(),vs->GetBufferSize()},pixel{ps->GetBufferPointer(),ps->GetBufferSize()};
    blend_desc blend{};blend.blend_enable[0]=true;blend.source_color_blend_factor[0]=blend_factor::source_alpha;blend.dest_color_blend_factor[0]=blend_factor::one_minus_source_alpha;blend.dest_alpha_blend_factor[0]=blend_factor::one_minus_source_alpha;
    rasterizer_desc raster{};raster.cull_mode=cull_mode::none;raster.scissor_enable=true;
    depth_stencil_desc depth{};depth.depth_enable=false;depth.depth_write_mask=false;depth.stencil_enable=false;
    primitive_topology topology=primitive_topology::triangle_list;uint32_t sampleCount=1,viewportCount=1;
    pipeline_subobject parts[]={{pipeline_subobject_type::vertex_shader,1,&vertex},{pipeline_subobject_type::pixel_shader,1,&pixel},{pipeline_subobject_type::blend_state,1,&blend},{pipeline_subobject_type::rasterizer_state,1,&raster},{pipeline_subobject_type::depth_stencil_state,1,&depth},{pipeline_subobject_type::primitive_topology,1,&topology},{pipeline_subobject_type::render_target_formats,1,&f},{pipeline_subobject_type::sample_count,1,&sampleCount},{pipeline_subobject_type::viewport_count,1,&viewportCount}};
    if(!device->create_pipeline(layout,uint32_t(std::size(parts)),parts,&pipeline))return fail("ui graphics pipeline failed");
    return true;
}
bool Renderer::draw(effect_runtime* runtime,command_list* list,resource_view target,const std::vector<Quad>& quads,HMODULE module){
    auto d=runtime->get_device();auto f=d->get_resource_view_desc(target).format;
    if(device!=d||targetFormat!=f){release();if(!initialize(d,f,module))return false;}
    if(!pipeline.handle&&!initialize(d,f,module))return false;
    uint32_t width=0,height=0;runtime->get_screenshot_width_and_height(&width,&height);if(!width||!height)return false;
    list->bind_render_targets_and_depth_stencil(1,&target,{});list->bind_pipeline(pipeline_stage::all_graphics,pipeline);
    viewport view{0,0,float(width),float(height),0,1};list->bind_viewports(0,1,&view);
    descriptor_table_update image{};image.count=1;image.type=descriptor_type::shader_resource_view;image.descriptors=&atlasView;list->push_descriptors(shader_stage::pixel,layout,1,image);
    descriptor_table_update sample{};sample.count=1;sample.type=descriptor_type::sampler;sample.descriptors=&sampler;list->push_descriptors(shader_stage::pixel,layout,2,sample);
    for(const auto& q:quads){rect scissor{std::clamp(int(std::floor(q.clip.x)),0,int(width)),std::clamp(int(std::floor(q.clip.y)),0,int(height)),std::clamp(int(std::ceil(q.clip.x+q.clip.w)),0,int(width)),std::clamp(int(std::ceil(q.clip.y+q.clip.h)),0,int(height))};if(scissor.right<=scissor.left||scissor.bottom<=scissor.top)continue;list->bind_scissor_rects(0,1,&scissor);
        float data[16]={q.rect.x/width*2-1,1-q.rect.y/height*2,q.rect.w/width*2,-q.rect.h/height*2,q.uv.x,q.uv.y,q.uv.w,q.uv.h};std::copy(q.top.begin(),q.top.end(),data+8);std::copy(q.bottom.begin(),q.bottom.end(),data+12);list->push_constants(shader_stage::all_graphics,layout,0,0,16,data);list->draw(6,1,0,0);
    }return true;
}
}
