#include "rr_menu.h"
#include "frame_generation.h"
#include <imgui.h>
#include <reshade.hpp>
#include "menu_input.h"
#include "resolution_modes.h"
#include <string>
#include <cctype>
#include <mmsystem.h>
#include "chungus_ui.h"
#include "chungus_ui_renderer.h"
#include "haunted_picker.h"
#include "user_settings.h"
#include <unordered_map>
#include <memory>
#include <chrono>
#include <cstdarg>
#include <cstdio>
bool rrGameInputV2Initialize();
unsigned rrGameInputV2Reads();
unsigned rrGameInputV2AllReads();
unsigned rrGameInputV2Blocked();
#ifdef CHUNGUSWARE_TEST_CONTROL
#include <filesystem>
#include <fstream>
#include <chrono>
#include <atomic>
std::atomic<unsigned> cwDraws{0};
extern "C" void ChungusWareTestDiagnostics();
void rrTestControlPoll(){
    static auto previous=std::chrono::steady_clock::time_point{};auto now=std::chrono::steady_clock::now();if(now-previous<std::chrono::seconds(1))return;previous=now;
    static unsigned applied=0;wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto directory=std::filesystem::path(exe).parent_path();
    unsigned sequence=0,renderer=0,count=0,pacing=0;int quality=-1;float target=0;
    std::ifstream control(directory/L"ChungusWare-test-control.txt");if(!(control>>sequence>>quality>>renderer>>count>>pacing>>target))return;
    auto rr=BedrockRrMenuState();if(sequence!=applied&&rr.width&&renderer<=2&&count<=5&&pacing<=2&&(quality==-1||quality==2)){
        auto module=GetModuleHandleW(L"bedrock_rr_quality.dll");auto select=module?reinterpret_cast<bool(*)(int)>(GetProcAddress(module,"NgxQualitySelect")):nullptr;
        if(select&&select(quality)){BedrockRrMenuSet(renderer,rr.domain);fgPacing(pacing,target);fgSelect(count);applied=sequence;}
    }
    static unsigned polls=0;if(++polls%10==0)ChungusWareTestDiagnostics();
    auto f=fgState();auto d=fgDeliveryState();DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    std::ofstream(directory/L"fg"/L"test-measurements.json")<<"{\"sequence\":"<<applied<<",\"menu_draws\":"<<cwDraws.load()<<",\"foreground_pid\":"<<foreground<<",\"pid\":"<<GetCurrentProcessId()<<",\"requested\":"<<f.requested<<",\"managed\":"<<f.managed<<",\"rendered_fps\":"<<f.renderedFps<<",\"generated_fps\":"<<d.generatedFps<<",\"delivered_fps\":"<<d.presentedFps<<",\"rg_ms\":"<<d.realToGeneratedMs<<",\"gr_ms\":"<<d.generatedToRealMs<<",\"mean_ms\":"<<d.meanMs<<",\"p99_ms\":"<<d.p99Ms<<",\"worst_ms\":"<<d.worstMs<<",\"unknown\":"<<d.unknownFrames<<",\"unmatched\":"<<d.unmatched<<",\"dropped_gen\":"<<d.droppedGenerated<<",\"event_age\":"<<d.ageSeconds<<"}";
}
#endif
namespace {
bool opened=false,debugOpen=false;HMODULE soundModule=nullptr;
bool rememberSettings=false,preferencesLoaded=false,restoreBase=false,restoreQuality=false,restoreFg=false;
chungus_ui::UserSettings preferences;
std::filesystem::path preferencesPath;
std::string preferencesWarning;
void restorePreferences(){
    if(!preferencesLoaded){
        preferencesLoaded=true;wchar_t directory[32768]{};
        auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",directory,32768);
        if(size&&size<32768){
            preferencesPath=std::filesystem::path(directory)/L"ChungusWare"/L"settings.ini";
            std::error_code error;
            if(std::filesystem::exists(preferencesPath,error)&&!chungus_ui::loadSettings(preferencesPath,preferences))preferencesWarning="saved settings invalid; startup defaults retained";
        }else preferencesWarning="local preferences directory unavailable";
        rememberSettings=preferences.remember;
        restoreBase=restoreQuality=restoreFg=rememberSettings;
    }
    if(restoreBase){BedrockRrMenuSet(preferences.renderer,preferences.domain);fgPacing(preferences.pacing,preferences.target);restoreBase=false;}
    if(restoreQuality){auto module=GetModuleHandleW(L"bedrock_rr_quality.dll");auto select=module?reinterpret_cast<bool(*)(int)>(GetProcAddress(module,"NgxQualitySelect")):nullptr;if(select&&select(preferences.resolution))restoreQuality=false;}
    // FG needs the world/runtime capability query before restoring its multiplier.
    if(restoreFg&&(preferences.fg==0||BedrockRrMenuState().width)){auto state=fgState();fgSelect(std::min(unsigned(preferences.fg),state.maximum));restoreFg=false;}
}
// Only a static pool and random seed: no machine/user identifiers or clock data.
std::string rabbitMessage="inventory complete.";
unsigned rabbitTier=0;
void nextHauntedMessage(){static chungus_ui::HauntedPicker picker(std::random_device{}());const auto& chosen=picker.next();rabbitMessage=chosen.text;rabbitTier=chosen.tier;}
std::string lowercase(const char* text){std::string result=text?text:"";for(char& c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return result;}
struct RuntimeUi {chungus_ui::Panel panel;chungus_ui::Renderer renderer;bool failed=false;};
std::unordered_map<reshade::api::effect_runtime*,std::unique_ptr<RuntimeUi>> interfaces;
const int resolutionIds[]={cw_resolution::FraudulentDlaa,2,-1,0,3,cw_resolution::Caca};
std::string format(const char* pattern,...){char buffer[256]{};va_list args;va_start(args,pattern);vsnprintf(buffer,sizeof(buffer),pattern,args);va_end(args);return buffer;}
void capture(reshade::api::effect_runtime* runtime){
    if(opened||debugOpen){runtime->block_input_next_frame();ImGui::GetIO().WantCaptureMouse=true;ImGui::GetIO().WantCaptureKeyboard=true;ImGui::SetNextFrameWantCaptureMouse(true);ImGui::SetNextFrameWantCaptureKeyboard(true);ImGui::GetIO().MouseDrawCursor=debugOpen;}
    if(debugOpen){bool visible=ImGui::Begin("ChungusWare developer recovery",&debugOpen);if(visible){auto state=BedrockRrMenuState();ImGui::Text("rr active: %d / evaluations: %u",state.rrActive,state.evaluations);ImGui::Text("gameinput v2 reads: %u / blocked: %u",rrGameInputV2Reads(),rrGameInputV2Blocked());for(auto& [key,ui]:interfaces)ImGui::TextWrapped("ui renderer: %s",ui->renderer.error.empty()?"ready":ui->renderer.error.c_str());ImGui::TextUnformatted("ctrl + shift + f8 closes this hidden panel");}ImGui::End();}
}
void draw(reshade::api::effect_runtime* runtime){
    restorePreferences();
    static bool v2Ready=false;if(!v2Ready)v2Ready=rrGameInputV2Initialize();
#ifdef CHUNGUSWARE_TEST_CONTROL
    cwDraws++;
#endif
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId()){opened=false;debugOpen=false;menu_input::setActive(false);fgPause(false);return;}
    auto& ptr=interfaces[runtime];if(!ptr)ptr=std::make_unique<RuntimeUi>();auto& ui=*ptr;
    if(runtime->is_key_pressed(VK_F8)){
        if(runtime->is_key_down(VK_CONTROL)&&runtime->is_key_down(VK_SHIFT))debugOpen=!debugOpen;
        else{opened=!opened;if(opened)nextHauntedMessage();ui.panel.resetInteraction();}
    }
    if(runtime->is_key_pressed(VK_ESCAPE)){opened=false;debugOpen=false;ui.panel.resetInteraction();}
    fgPause(false);menu_input::setActive(opened||debugOpen);
    if(!opened)return;runtime->block_input_next_frame();
    auto state=BedrockRrMenuState();auto fg=fgState();auto delivery=fgDeliveryState();
    auto module=GetModuleHandleW(L"bedrock_rr_quality.dll");auto get=module?reinterpret_cast<int(*)()>(GetProcAddress(module,"NgxQualityGet")):nullptr;auto select=module?reinterpret_cast<bool(*)(int)>(GetProcAddress(module,"NgxQualitySelect")):nullptr;
    chungus_ui::Model model;int nativeQuality=get?get():-1;for(int i=0;i<6;i++)if(resolutionIds[i]==nativeQuality)model.resolution=i;
    model.renderer=int(state.mode);model.fg=int(fg.requested);model.maximumFg=int(fg.maximum);model.pacing=int(fg.pacingMode);model.target=fg.targetFps;model.domain=state.domain==2?0:state.domain==1?1:2;model.rrActive=state.rrActive;model.reflex=fg.requested>0;model.status=lowercase(fg.status);
    if(state.width&&state.outputWidth)model.dimensions=format("%u x %u -> %u x %u",state.width,state.height,state.outputWidth,state.outputHeight);
    model.diagnostics={format("rr: %s / preset f",state.rrActive?"active":"fallback"),format("rr evaluations: %u",state.evaluations),format("rendered: %.1f fps",fg.renderedFps),format("delivered: %.1f fps",delivery.presentedFps),format("generated: %.1f fps",delivery.generatedFps),format("avg: %.3f ms",delivery.meanMs),format("p99: %.3f ms",delivery.p99Ms),format("worst: %.3f ms",delivery.worstMs),format("real > gen: %.3f ms",delivery.realToGeneratedMs),format("gen > real: %.3f ms",delivery.generatedToRealMs),format("drops: %u / unmatched: %u",delivery.droppedGenerated,delivery.unmatched),format("display: %.0f hz",fg.refreshHz),"reflex: partial / auto with fg",format("mouse v2: %s",v2Ready?"ready":"waiting"),format("blocked samples: %u",rrGameInputV2Blocked())};
    uint32_t width=0,height=0;runtime->get_screenshot_width_and_height(&width,&height);if(!width||!height)return;
    chungus_ui::Input input;uint32_t x=0,y=0;runtime->get_mouse_cursor_position(&x,&y);input.x=float(x);input.y=float(y);input.down=runtime->is_mouse_button_down(0);input.pressed=runtime->is_mouse_button_pressed(0);input.released=runtime->is_mouse_button_released(0);input.tab=runtime->is_key_pressed(VK_TAB);input.shift=runtime->is_key_down(VK_SHIFT);input.enter=runtime->is_key_pressed(VK_RETURN)||runtime->is_key_pressed(VK_SPACE);input.up=runtime->is_key_pressed(VK_UP);input.below=runtime->is_key_pressed(VK_DOWN);input.time=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    model.rabbitMessage=rabbitMessage;model.rabbitTier=rabbitTier;model.remember=rememberSettings;
    if(!preferencesWarning.empty())model.status=preferencesWarning;
    const auto previous=model;auto actions=ui.panel.frame(model,input,width,height);
    if(actions.apply){
        bool accepted=true;if(model.resolution!=previous.resolution)accepted=select&&select(resolutionIds[model.resolution]);
        if(accepted&&model.resolution!=previous.resolution&&resolutionIds[model.resolution]==cw_resolution::Caca )PlaySoundW(MAKEINTRESOURCEW(101),soundModule,SND_RESOURCE|SND_ASYNC|SND_NODEFAULT);
        if(model.renderer!=previous.renderer||model.domain!=previous.domain)BedrockRrMenuSet(unsigned(model.renderer),model.domain==0?2:model.domain==1?1:0);
        if(model.pacing!=previous.pacing||model.target!=previous.target)fgPacing(unsigned(model.pacing),model.target);
        if(model.fg!=previous.fg)fgSelect(unsigned(model.fg));
        if(!accepted)model.resolution=previous.resolution;
    }
    bool settingsChanged=actions.apply||model.remember!=previous.remember;
    if(settingsChanged){
        restoreBase=restoreQuality=restoreFg=false;rememberSettings=model.remember;
        if(rememberSettings||previous.remember){
            chungus_ui::UserSettings saved;saved.remember=rememberSettings;saved.resolution=resolutionIds[model.resolution];saved.renderer=model.renderer;saved.domain=model.domain==0?2:model.domain==1?1:0;saved.fg=model.fg;saved.pacing=model.pacing;saved.target=model.target;
            if(chungus_ui::saveSettings(preferencesPath,saved))preferencesWarning.clear();else preferencesWarning="settings could not be saved; check local file permissions";
        }
    }
    if(!ui.failed&&!ui.renderer.present(runtime,ui.panel.quads(),soundModule)){ui.failed=true;OutputDebugStringA(("ChungusWare UI: "+ui.renderer.error+"\n").c_str());debugOpen=true;}
    if(actions.close){opened=false;ui.panel.resetInteraction();fgPause(false);menu_input::setActive(debugOpen);}
}
void destroy(reshade::api::effect_runtime* runtime){auto found=interfaces.find(runtime);if(found!=interfaces.end()){found->second->renderer.release();interfaces.erase(found);}opened=false;debugOpen=false;menu_input::setActive(false);fgPause(false);}
}
bool rrMenuInitialize(HMODULE module){soundModule=module;if(!reshade::register_addon(module,GetModuleHandleW(L"dxgi.dll")))return false;menu_input::initialize();reshade::register_event<reshade::addon_event::reshade_present>(&draw);reshade::register_event<reshade::addon_event::reshade_overlay>(&capture);reshade::register_event<reshade::addon_event::destroy_effect_runtime>(&destroy);fgInitialize();return true;}
void rrMenuShutdown(HMODULE module){menu_input::shutdown();reshade::unregister_event<reshade::addon_event::reshade_present>(&draw);reshade::unregister_event<reshade::addon_event::reshade_overlay>(&capture);reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(&destroy);for(auto& [runtime,ui]:interfaces)ui->renderer.release();interfaces.clear();reshade::unregister_addon(module);}
