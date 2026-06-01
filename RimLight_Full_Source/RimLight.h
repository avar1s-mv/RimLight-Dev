#pragma once

#ifndef RIMLIGHT_H
#define RIMLIGHT_H

#include "AEConfig.h"

#ifdef AE_OS_WIN
    #include <Windows.h>
#endif

#ifndef DllExport
    #ifdef AE_OS_WIN
        #define DllExport __declspec(dllexport)
    #else
        #define DllExport
    #endif
#endif

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#include <cmath>

#include "RimLight_Strings.h"

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION  1

#define RIMLIGHT_NAME "RimLight"
#define RIMLIGHT_CATEGORY "Avaris"

// Parameter controls
#define RIM_SIZE_MIN      0.0
#define RIM_SIZE_MAX      20.0
#define RIM_SIZE_DFLT     4.0

#define RIM_INTENSITY_MIN 0.0
#define RIM_INTENSITY_MAX 100.0
#define RIM_INTENSITY_DFLT 35.0

#define RIM_CONTRAST_MIN  0.0
#define RIM_CONTRAST_MAX  100.0
#define RIM_CONTRAST_DFLT 25.0

#define RIM_ANGLE_MIN     0.0
#define RIM_ANGLE_MAX     360.0
#define RIM_ANGLE_DFLT    315.0

#define RIM_OPACITY_MIN   0.0
#define RIM_OPACITY_MAX   100.0
#define RIM_OPACITY_DFLT  100.0

enum RimLightParams {
    RIM_INPUT = 0,
    RIM_TYPE,
    RIM_BLEND_MODE,
    RIM_COLOR,
    RIM_SIZE,
    RIM_INTENSITY,
    RIM_CONTRAST,
    RIM_ANGLE,
    RIM_OPACITY,
    RIM_NUM_PARAMS
};

enum RimTypePopup {
    RIM_TYPE_SINGLE = 1,
    RIM_TYPE_BOTH_SIDES = 2
};

enum RimBlendModePopup {
    RIM_BLEND_NORMAL = 1,
    RIM_BLEND_DARKEN,
    RIM_BLEND_MULTIPLY,
    RIM_BLEND_COLOR_BURN,
    RIM_BLEND_OVERLAY,
    RIM_BLEND_ADD
};

typedef struct RimLightInfo {
    A_long      rim_type;
    A_long      rim_blend_mode;
    PF_Pixel    rim_color;
    PF_FpLong   rim_size;
    PF_FpLong   rim_intensity;
    PF_FpLong   rim_contrast;
    PF_FpLong   rim_angle;
    PF_FpLong   rim_opacity;
} RimLightInfo, *RimLightInfoP, **RimLightInfoH;

#ifdef __cplusplus
extern "C" {
#endif

DllExport PF_Err EffectMain(
    PF_Cmd      cmd,
    PF_InData  *in_data,
    PF_OutData *out_data,
    PF_ParamDef *params[],
    PF_LayerDef *output,
    void        *extra);

#ifdef __cplusplus
}
#endif

// String helpers
char *GetStringPtr(int strNum);
#define STR(StrID) (GetStringPtr((StrID)))

#endif // RIMLIGHT_H
