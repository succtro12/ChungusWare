#pragma once
#include "chungus_ui.h"
#include <reshade.hpp>
#include <unordered_map>
namespace chungus_ui {
class Renderer {
public:
    bool draw(reshade::api::effect_runtime*,reshade::api::command_list*,reshade::api::resource_view,const std::vector<Quad>&,HMODULE);
    bool present(reshade::api::effect_runtime*,const std::vector<Quad>&,HMODULE);
    void release();std::string error;
private:
    reshade::api::device* device=nullptr;
    reshade::api::pipeline_layout layout{};reshade::api::pipeline pipeline{};
    reshade::api::resource atlas{};reshade::api::resource_view atlasView{};reshade::api::sampler sampler{};
    reshade::api::format targetFormat=reshade::api::format::unknown;
    std::unordered_map<uint64_t,reshade::api::resource_view> targets;
    bool initialize(reshade::api::device*,reshade::api::format,HMODULE);
};
}
