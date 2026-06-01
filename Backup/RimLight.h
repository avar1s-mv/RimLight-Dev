#pragma once

#ifndef RIMLIGHT_H
#define RIMLIGHT_H

#include "AEConfig.h"

#ifdef AE_OS_WIN
#ifndef WINDOWS_IGNORE_PACKING_MISMATCH
#define WINDOWS_IGNORE_PACKING_MISMATCH
#endif
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
#ifndef PF_TABLE_BITS
#define PF_TABLE_BITS 8
#endif
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#include <cmath>

#include "RimLight_Strings.h"

#define MAJOR_VERSION   1
#define MINOR_VERSION   1
#define BUG_VERSION     0
#define STAGE_VERSION   PF_Stage_RELEASE
#define BUILD_VERSION   1

#define RIMLIGHT_NAME       "RimLight"
#define RIMLIGHT_CATEGORY   "Avaris"

// ── Rim Size ──────────────────────────────────────────
#define RIM_SIZE_MIN    0.0
#define RIM_SIZE_MAX    20.0
#define RIM_SIZE_DFLT   4.0

// ── Rim Intensity ──────────────────────────────────────
#define RIM_INTENSITY_MIN   0.0
#define RIM_INTENSITY_MAX   100.0
#define RIM_INTENSITY_DFLT  35.0

// ── Rim Contrast ──────────────────────────────────────
#define RIM_CONTRAST_MIN    0.0
#define RIM_CONTRAST_MAX    100.0
#define RIM_CONTRAST_DFLT   25.0

// ── Rim Angle ─────────────────────────────────────────
#define RIM_ANGLE_DFLT  315.0

// ── Rim Opacity ───────────────────────────────────────
#define RIM_OPACITY_MIN     0.0
#define RIM_OPACITY_MAX     100.0
#define RIM_OPACITY_DFLT    100.0

// ── Darken ────────────────────────────────────────────
#define DARKEN_AMOUNT_MIN   0.0
#define DARKEN_AMOUNT_MAX   100.0
#define DARKEN_AMOUNT_DFLT  0.0

// ── Glow ──────────────────────────────────────────────
#define GLOW_THRESHOLD_MIN      0.0
#define GLOW_THRESHOLD_MAX      100.0
#define GLOW_THRESHOLD_DFLT     20.0

#define GLOW_RADIUS_MIN     0.0
#define GLOW_RADIUS_MAX     50.0
#define GLOW_RADIUS_DFLT    3.0

#define GLOW_INTENSITY_MIN  0.0
#define GLOW_INTENSITY_MAX  100.0
#define GLOW_INTENSITY_DFLT 0.0

#define GLOW_OPACITY_MIN    0.0
#define GLOW_OPACITY_MAX    100.0
#define GLOW_OPACITY_DFLT   100.0

// ── Parameter indices ─────────────────────────────────
#ifdef RIM_INPUT
#undef RIM_INPUT
#endif

enum RimLightParams {
    RIM_INPUT = 0,

    // Rim Light section
    RIM_TYPE,
    RIM_BLEND_MODE,
    RIM_COLOR,
    RIM_SIZE,
    RIM_INTENSITY,
    RIM_CONTRAST,
    RIM_ANGLE,
    RIM_OPACITY,

    // Darken section
    DARKEN_DISK_ID,         // group start
    DARKEN_AMOUNT,
    DARKEN_GROUP_END,       // group end

    // Glow section
    GLOW_DISK_ID,           // group start
    GLOW_THRESHOLD,
    GLOW_RADIUS,
    GLOW_INTENSITY,
    GLOW_COLOR,
    GLOW_OPACITY,
    GLOW_GROUP_END,         // group end

    RIM_NUM_PARAMS
};

enum RimTypePopup {
    RIM_TYPE_SINGLE = 1,
    RIM_TYPE_BOTH_SIDES = 2
};

enum RimBlendModePopup {
    RIM_BLEND_NORMAL = 1,
    RIM_BLEND_ADD
};

typedef struct RimLightInfo {
    // Rim
    A_long      rim_type;
    A_long      rim_blend_mode;
    PF_Pixel    rim_color;
    PF_FpLong   rim_size;
    PF_FpLong   rim_intensity;
    PF_FpLong   rim_contrast;
    PF_FpLong   rim_angle;
    PF_FpLong   rim_opacity;

    // Darken
    PF_FpLong   darken_amount;

    // Glow
    PF_FpLong   glow_threshold;
    PF_FpLong   glow_radius;
    PF_FpLong   glow_intensity;
    PF_Pixel    glow_color;
    PF_FpLong   glow_opacity;
} RimLightInfo, * RimLightInfoP, ** RimLightInfoH;

#ifdef __cplusplus
extern "C" {
#endif

    DllExport PF_Err EffectMain(
        PF_Cmd      cmd,
        PF_InData* in_data,
        PF_OutData* out_data,
        PF_ParamDef* params[],
        PF_LayerDef* output,
        void* extra);

#ifdef __cplusplus
}
#endif

char* GetStringPtr(int strNum);
#ifndef STR
#define STR(StrID) (GetStringPtr((StrID)))
#endif

#endif // RIMLIGHT_H