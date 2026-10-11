#include "game_input_v2_gate.cpp"
#include <iostream>
#include <stdexcept>
unsigned stage=0;
void require(bool condition){stage++;if(!condition)throw std::runtime_error("GameInput regression at check "+std::to_string(stage));}
void cursorAlignmentRegression(){
    game_input_v2_gate::Position p;
    GameInputMouseState s{};
    s.positions=GameInputMouseAbsolutePosition|GameInputMouseRelativePosition;
    s.positionX=100;s.positionY=50;s.absolutePositionX=900;s.absolutePositionY=300;
    game_input_v2_gate::filter(p,&s,false,0);
    s.positionX=200;s.positionY=90;s.absolutePositionX=960;s.absolutePositionY=270;
    game_input_v2_gate::filter(p,&s,true,1);
    require(s.positionX==100&&s.positionY==50&&s.absolutePositionX==900&&s.absolutePositionY==300);
    s.positionX=300;s.positionY=120;s.absolutePositionX=1010;s.absolutePositionY=240;
    game_input_v2_gate::filter(p,&s,false,2);
    // Closing suppresses accumulated camera motion, but absolute hit testing
    // must immediately use the actual cursor, not an offset virtual cursor.
    require(s.absolutePositionX==1010&&s.absolutePositionY==240);
    require(s.positionX==100&&s.positionY==50);
    s.positionX=305;s.positionY=123;s.absolutePositionX=1015;s.absolutePositionY=243;
    game_input_v2_gate::filter(p,&s,false,2);
    require(s.positionX==105&&s.positionY==53&&s.absolutePositionX==1015&&s.absolutePositionY==243);

    // Include negative desktop coordinates, wheel movement, two independent
    // devices and 10,000 capture cycles. No real input or cursor warping.
    for(unsigned device=0;device<2;device++){
        game_input_v2_gate::Position position;
        int64_t rawX=0,rawY=0,wheel=0,deliveredX=0,deliveredY=0,deliveredWheel=0;
        auto sample=[&](bool captured,unsigned epoch,int64_t x,int64_t y){
            GameInputMouseState state{};
            state.positions=s.positions;state.positionX=rawX;state.positionY=rawY;
            state.wheelY=wheel;state.absolutePositionX=x;state.absolutePositionY=y;
            state.buttons=GameInputMouseLeftButton;
            game_input_v2_gate::filter(position,&state,captured,epoch);
            require(state.positionX==deliveredX&&state.positionY==deliveredY&&state.wheelY==deliveredWheel);
            require(state.positions==s.positions);
            require(state.buttons==(captured?GameInputMouseNone:GameInputMouseLeftButton));
            if(!captured)require(state.absolutePositionX==x&&state.absolutePositionY==y);
            return state;
        };
        sample(false,0,-1500,200);
        for(unsigned cycle=0;cycle<10000;cycle++){
            unsigned open=cycle*2+1,close=open+1;
            rawX+=100;rawY-=50;wheel+=4;
            auto frozen=sample(true,open,-900+int64_t(cycle),400);
            rawX+=300;rawY+=120;wheel+=8;
            auto still=sample(true,open,-400+int64_t(cycle),170);
            require(still.absolutePositionX==frozen.absolutePositionX&&still.absolutePositionY==frozen.absolutePositionY);
            rawX+=7;rawY-=9;wheel++;
            sample(false,close,-1200+int64_t(cycle),120);
            rawX+=5;rawY+=3;wheel++;
            deliveredX+=5;deliveredY+=3;deliveredWheel++;
            sample(false,close,-1195+int64_t(cycle),123);
        }
        // A complete open/close between readings must not leave coordinates stale.
        rawX+=80;sample(false,20002,321,654);
    }
}
int main(int argc,char** argv){try{
    cursorAlignmentRegression();
    if(argc==2&&!strcmp(argv[1],"--filter-only")){
        std::cout<<"PASS: absolute cursor alignment, capture freeze, camera/wheel continuity, two devices and 20,000 F8 cycles\n";return 0;
    }
    game_input_v2_gate::Position p;GameInputMouseState state{};
    state.positionX=100;state.positionY=50;game_input_v2_gate::filter(p,&state,false,0);
    state.positionX=200;state.positionY=90;state.wheelY=10;state.buttons=GameInputMouseLeftButton;game_input_v2_gate::filter(p,&state,true,1);
    require(state.positionX==100&&state.positionY==50&&state.wheelY==0&&state.buttons==GameInputMouseNone);
    state.positionX=300;state.positionY=120;state.wheelY=20;game_input_v2_gate::filter(p,&state,true,1);require(state.positionX==100);
    state.positionX=400;state.positionY=160;state.wheelY=30;game_input_v2_gate::filter(p,&state,false,2);require(state.positionX==100&&state.positionY==50);
    state.positionX=405;state.positionY=163;state.wheelY=31;game_input_v2_gate::filter(p,&state,false,2);require(state.positionX==105&&state.positionY==53&&state.wheelY==1);
    auto module=LoadLibraryExW(L"GameInputRedist.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);require(module!=nullptr);auto create=reinterpret_cast<HRESULT(WINAPI*)(IUnknown**)>(GetProcAddress(module,"GameInputCreate"));require(create!=nullptr);
    Microsoft::WRL::ComPtr<IUnknown> legacy;require(SUCCEEDED(create(&legacy)));Microsoft::WRL::ComPtr<IGameInput> input;require(SUCCEEDED(legacy.As(&input)));Microsoft::WRL::ComPtr<IGameInputReading> reading;
    for(unsigned i=0;i<100&&!reading;i++){input->GetCurrentReading(GameInputKindMouse,nullptr,&reading);if(!reading)Sleep(10);}
    require(reading!=nullptr);bool ready=false;for(unsigned i=0;i<100&&!ready;i++){ready=rrGameInputV2Initialize();if(!ready)Sleep(10);}require(ready);
    require(reading->GetMouseState(&state));auto before=rrGameInputV2Blocked();menu_input::setActive(true);require(reading->GetMouseState(&state));require(rrGameInputV2Blocked()==before+1&&state.buttons==GameInputMouseNone);menu_input::setActive(false);require(reading->GetMouseState(&state));
    std::cout<<"PASS: GameInput v2 ABI, native mouse hook, freeze, wheel/buttons, and no accumulated movement on close\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
