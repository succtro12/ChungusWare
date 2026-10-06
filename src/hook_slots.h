#pragma once
#include <stddef.h>
typedef struct HookSlots {
    size_t compute,root,cbv,srv,uav,copy,copySimple;
    size_t reset,dispatch,pipeline,heaps,setRoot,table,constant,constants,rootCbv;
    size_t stream,stateObject,rayPipeline,rays,map,unmap,barriers,execute;
    size_t committed,placed;
    size_t committed1,committed2,placed1;
} HookSlots;
#ifdef __cplusplus
extern "C" {
#endif
const HookSlots* BedrockRrHookSlots(void);
#ifdef __cplusplus
}
#endif
