#pragma once
#include "nvsdk_ngx.h"
void srQualityPoll();
void srQualityBeforeCreate(NVSDK_NGX_Feature feature,NVSDK_NGX_Parameter* parameters,int& originalQuality,bool& changed);
void srQualityAfterCreate(NVSDK_NGX_Parameter* parameters,int originalQuality,bool changed);
