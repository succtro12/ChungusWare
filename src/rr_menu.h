#pragma once
#include <windows.h>
struct RrMenuState { unsigned mode,domain,width,height,outputWidth,outputHeight,evaluations; bool rrActive; };
extern "C" RrMenuState BedrockRrMenuState();
extern "C" void BedrockRrMenuSet(unsigned mode,unsigned domain);
bool rrMenuInitialize(HMODULE module);
void rrMenuShutdown(HMODULE module);
