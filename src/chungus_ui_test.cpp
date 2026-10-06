#include "chungus_ui.h"
#include "haunted_picker.h"
#include "ui_font_metrics.h"
#include "system_ui_font.h"
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <filesystem>
using namespace chungus_ui;
int main(int argc,char** argv){try{
    static_assert(hauntedMessages.size()==6666);
    std::unordered_set<std::string_view> catalogue;
    for(const auto& message:hauntedMessages){
        auto line=message.text;
        if(!catalogue.insert(line).second)throw std::runtime_error("duplicate haunted caption");
        auto wrapped=wrapHauntedCaption(std::string(line));unsigned rows=1;float width=0;
        for(unsigned char c:wrapped){if(c=='\n'){rows++;width=0;}else{width+=uiFontSpacing()[c-32]*.85f;if(width>223.01f)throw std::runtime_error("caption exceeds panel width");}}
        if(rows>3)throw std::runtime_error("caption exceeds three-line panel height: "+std::string(line));
        if(hauntedRare(message.tier)!=(message.tier>=3&&message.tier<=4))throw std::runtime_error("rare selection mapping");
        if(hauntedBlackRed(message.tier)!=(message.tier==4))throw std::runtime_error("breach-only style mapping");
    }
    HauntedPicker picker(6666);std::vector<const HauntedMessage*> history;std::array<unsigned,5> counts{};
    for(unsigned n=0;n<100000;n++){
        const auto& message=picker.next();unsigned familyCount=0;
        if(!catalogue.count(message.text))throw std::runtime_error("unknown catalogue entry");
        for(size_t j=0;j<history.size();j++){
            if(history[j]->id==message.id)throw std::runtime_error("64-entry exact history violation");
            if(j<8&&history[j]->family==message.family)familyCount++;
        }
        if(familyCount>=2)throw std::runtime_error("8-entry family history violation");
        if(!history.empty()&&hauntedRare(history[0]->tier)&&hauntedRare(message.tier))throw std::runtime_error("consecutive rare messages");
        history.insert(history.begin(),&message);if(history.size()>64)history.pop_back();counts[message.tier]++;
    }
    const unsigned expected[]={4200,1800,600,60,6};
    for(unsigned t=0;t<5;t++){
        double proportion=double(expected[t])/6666.0;
        double sigma=std::sqrt(100000*proportion*(1-proportion));
        if(std::abs(double(counts[t])-100000*proportion)>6*sigma)throw std::runtime_error("rarity outside statistical bound");
    }
    if(argc>3){std::ofstream out(argv[3]);out<<"{\"selections\":100000,\"tier_counts\":[";for(unsigned t=0;t<5;t++){if(t)out<<',';out<<counts[t];}out<<"],\"history_violations\":0,\"family_violations\":0,\"consecutive_rare\":0,\"seed\":6666}\n";}
    std::cout<<"Haunting checks passed: 6666 unique static lines, all fit, 100000 bounded-history picks. Tier counts:";for(auto n:counts)std::cout<<' '<<n;std::cout<<'\n';
    Panel p;Model m;m.rrActive=true;m.dimensions="1707 x 960 -> 2560 x 1440";
    Input i;auto frame=[&]{return p.frame(m,i,1920,1080);};frame();
    auto move=[&](float x,float y){i={};i.x=42+x*1.35f;i.y=42+y*1.35f;};
    auto click=[&](float x,float y){move(x,y);i.pressed=i.down=true;frame();i.pressed=i.down=false;i.released=true;return frame();};
    click(80,298);auto selected=click(80,330);if(m.resolution!=0||!selected.apply)throw std::runtime_error("dropdown did not immediately select fraudulent dlaa");
    click(80,298);click(50,150);if(m.renderer!=2)throw std::runtime_error("dropdown click changed underlying renderer");
    click(80,192);auto changed=click(80,227);if(m.renderer!=0||!changed.apply)throw std::runtime_error("renderer dropdown did not apply immediately");
    click(475,518);if(!m.remember)throw std::runtime_error("remember settings checkbox did not toggle");
    // The independent persistence tooltip stays black/red with an ordinary
    // message. Closing/resetting does not mutate the held message or tier.
    Model held;held.rabbitMessage="still warm.";held.rabbitTier=0;Panel heldPanel;
    Input hover;hover.x=42+475*1.35f;hover.y=42+518*1.35f;
    heldPanel.frame(held,hover,1920,1080);hover.time=1;heldPanel.frame(held,hover,1920,1080);
    bool tooltipBlack=false,tooltipRed=false;
    for(const auto& q:heldPanel.quads()){tooltipBlack|=q.top==Color{0,0,0,1};tooltipRed|=q.top==Color{1,0,0,1};}
    if(!tooltipBlack||!tooltipRed)throw std::runtime_error("independent remember-settings tooltip style regressed");
    if(held.rabbitMessage!="still warm."||held.rabbitTier!=0)throw std::runtime_error("panel rerolled held message");
    HauntedPicker openingPicker(1707);
    for(unsigned n=0;n<2048;n++){
        const auto& selected=openingPicker.next();held.rabbitMessage=selected.text;held.rabbitTier=selected.tier;
        auto stable=held.rabbitMessage;heldPanel.resetInteraction();hover={};
        for(unsigned frame=0;frame<3;frame++){heldPanel.frame(held,hover,1920,1080);if(held.rabbitMessage!=stable)throw std::runtime_error("visible panel message changed");}
        heldPanel.resetInteraction();
    }
    move(0,0);i.tab=true;frame();i.tab=false;i.enter=true;frame();i.enter=false;
    if(p.advanced())click(350,569);p.resetInteraction();click(350,569);if(!p.advanced())throw std::runtime_error("advanced page failed");
    Model preview;preview.rrActive=true;preview.dimensions="1707 x 960 -> 2560 x 1440";preview.diagnostics={"rr: active / preset f","rendered: 90.0 fps","delivered: 180.0 fps","generated: 90.0 fps","avg: 5.556 ms","p99: 5.700 ms","worst: 6.030 ms","real > gen: 5.556 ms","gen > real: 5.556 ms","drops: 0 / unmatched: 0","display: 180 hz","reflex: partial / auto","mouse v2: ready"};
    Panel picture;i={};picture.frame(preview,i,1060,920);
    auto dump=[&](const char* path){std::ofstream out(path);out<<"[";bool first=true;for(auto& q:picture.quads()){if(!first)out<<',';first=false;out<<"{\"rect\":["<<q.rect.x<<','<<q.rect.y<<','<<q.rect.w<<','<<q.rect.h<<"],\"uv\":["<<q.uv.x<<','<<q.uv.y<<','<<q.uv.w<<','<<q.uv.h<<"],\"clip\":["<<q.clip.x<<','<<q.clip.y<<','<<q.clip.w<<','<<q.clip.h<<"],\"top\":["<<q.top[0]<<','<<q.top[1]<<','<<q.top[2]<<','<<q.top[3]<<"],\"bottom\":["<<q.bottom[0]<<','<<q.bottom[1]<<','<<q.bottom[2]<<','<<q.bottom[3]<<"]}";}out<<"]";};
    if(argc>1)dump(argv[1]);
    // Forced tiers live only in this standalone developer executable, not the DLL.
    if(argc>4){std::filesystem::create_directories(argv[4]);for(unsigned tier=0;tier<5;tier++){
        auto found=std::find_if(hauntedMessages.begin(),hauntedMessages.end(),[&](const auto& message){return message.tier==tier;});
        preview.rabbitTier=tier;preview.rabbitMessage=found->text;i={};picture.frame(preview,i,1060,920);
        bool hasBlack=false,hasRed=false,hasCream=false;
        for(const auto& q:picture.quads()){
            if(std::abs(q.rect.x-(42+458*1.35f))<.01f&&std::abs(q.rect.y-(42+442*1.35f))<.01f&&std::abs(q.rect.h-62*1.35f)<.01f&&std::abs(q.rect.w-237*1.35f)<.01f){hasBlack=q.top==Color{0,0,0,1};hasCream=q.top==Color{1,1,.7f,1};}
            if(q.top==Color{1,0,0,1})hasRed=true;
        }
        if(tier==4?(!hasBlack||!hasRed||hasCream):(hasBlack||hasRed||!hasCream))throw std::runtime_error("message box styling mismatch tier="+std::to_string(tier)+" black="+std::to_string(hasBlack)+" red="+std::to_string(hasRed)+" cream="+std::to_string(hasCream));
        auto path=(std::filesystem::path(argv[4])/("tier-"+std::to_string(tier)+".json")).string();dump(path.c_str());
    }}

    i.x=42+350*1.35f;i.y=42+569*1.35f;i.down=i.pressed=true;picture.frame(preview,i,1060,920);i.down=i.pressed=false;i.released=true;picture.frame(preview,i,1060,920);i.released=false;picture.frame(preview,i,1060,920);if(argc>2)dump(argv[2]);
    std::cout<<"Custom UI interaction checks passed: immediate dropdown/checkbox changes, modal popup, keyboard focus, advanced page.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
