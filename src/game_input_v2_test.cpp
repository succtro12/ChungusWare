#include "game_input_v2_gate.cpp"
#include <iostream>
#include <stdexcept>
unsigned stage=0;
void require(bool condition){stage++;if(!condition)throw std::runtime_error("GameInput regression at check "+std::to_string(stage));}
int main(){try{
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
