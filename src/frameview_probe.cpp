#include <windows.h>
#include <vector>
#include <cstdio>
#include <fstream>
#include "FvSDK.h"
int wmain(int argc,wchar_t** argv) {
 if(argc<3)return 2; const auto pid=wcstoul(argv[1],nullptr,10);
 auto initialize=FvSDK_Initialize();printf("Initialize=%u\n",initialize);if(initialize!=FV_SUCCESS)return 1;
 auto create=&FvSDK_CreateSession;auto start=&FvSDK_StartSession;auto enable=&FvSDK_EnableMetrics;
 auto sample=&FvSDK_SampleData;auto read=&FvSDK_ReadData;auto stop=&FvSDK_StopSession;auto destroy=&FvSDK_DestroySession;
 FvSession session{}; auto result=create(&session);printf("CreateSession=%u\n",result);if(result!=FV_SUCCESS)return 1;
 FvMetricType metric=eFrame;result=enable(session,&metric,1);printf("EnableMetrics=%u\n",result);
 if(result==FV_SUCCESS){result=start(session);printf("StartSession=%u\n",result);}
 std::ofstream out(argv[2]);out<<"pid,swapchain,qpc,present_interval_ms,display_change_interval_ms,pc_latency_ms,frame_id\n";
 unsigned rows=0,errors=0;auto begin=GetTickCount64();
 while(result==FV_SUCCESS&&GetTickCount64()-begin<30000){Sleep(250);auto r=sample(session,FVSDK_SAMPLEDATA_DONOTWAIT);if(r!=FV_SUCCESS){if(errors++<4)printf("SampleData=%u\n",r);continue;}
  Samples samples;samples.type=eFrame;r=read(session,&samples,1);if(!samples.mNumSamples)continue;
  if(samples.mNumSamples>100000){puts("Invalid sample bound");break;}std::vector<Frame> frames(samples.mNumSamples);samples.mData=frames.data();r=read(session,&samples,1);if(r!=FV_SUCCESS){if(errors++<4)printf("ReadData=%u\n",r);continue;}
  for(size_t i=0;i<samples.mNumSamples;i++){auto& f=frames[i];if(f.PID!=pid)continue;out<<f.PID<<','<<f.SwapChain<<','<<f.mTimestamp<<','<<f.mMsBetweenPresents<<','<<f.mMsBetweenDisplayChange<<','<<f.mMsPCLatency<<','<<f.FrameID<<'\n';rows++;}
 }
 printf("Rows=%u Errors=%u\n",rows,errors);if(result==FV_SUCCESS)printf("StopSession=%u\n",stop(session));destroy(session);return rows?0:1;
}
