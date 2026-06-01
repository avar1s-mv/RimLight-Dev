#pragma once

#ifndef RIMLIGHT_H
#define RIMLIGHT_H

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "AE_EffectCBSuites.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include "String_Utils.h"
#include "Param_Utils.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Version
// ─────────────────────────────────────────────────────────────────────────────
#define MAJOR_VERSION    1
#define MINOR_VERSION    0
#define BUG_VERSION      0
#define STAGE_VERSION    PF_Stage_DEVELOP
#define BUILD_VERSION    1

// ─────────────────────────────────────────────────────────────────────────────
// Parameter indices
// ─────────────────────────────────────────────────────────────────────────────
enum {
    RIM_INPUT = 0,  // layer input (always index 0)
    RIM_COLOR,
    RIM_SIZE,
    RIM_OPACITY,
    RIM_ANGLE,
    RIM_SOFTNESS,
    RIM_BLEND_MODE,

    // ── Glow group ────────────────────────────────────────────────────────────
    GLOW_GROUP_START,       // PF_ADD_TOPIC  (group open)
    GLOW_THRESHOLD,
    GLOW_THRESHOLD_SMOOTH,
    GLOW_INDIVIDUAL_RADIUS, // checkbox: 1=individual X/Y mode, 0=uniform
    GLOW_RADIUS,            // uniform radius (shown when GLOW_INDIVIDUAL_RADIUS=0)
    GLOW_RADIUS_GROUP_START,// "Separate Radius" sub-group (shown when GLOW_INDIVIDUAL_RADIUS=1)
    GLOW_RADIUS_X,          // individual radius X
    GLOW_RADIUS_Y,          // individual radius Y
    GLOW_RADIUS_GROUP_END,
    GLOW_INTENSITY,
    GLOW_SATURATION,
    GLOW_COLOR,
    GLOW_OPACITY,
    GLOW_TYPE,
    GLOW_GROUP_END,         // PF_END_TOPIC  (group close)

    RIM_NUM_PARAMS
};

// Disk IDs (must be unique and stable — never change after shipping)
enum {
    RIM_COLOR_DISK_ID = 1,
    RIM_SIZE_DISK_ID,
    RIM_OPACITY_DISK_ID,
    RIM_ANGLE_DISK_ID,
    RIM_SOFTNESS_DISK_ID,
    RIM_BLEND_MODE_DISK_ID,
    GLOW_GROUP_START_DISK_ID,
    GLOW_THRESHOLD_DISK_ID,
    GLOW_THRESHOLD_SMOOTH_DISK_ID,
    GLOW_RADIUS_DISK_ID,
    GLOW_INTENSITY_DISK_ID,
    GLOW_SATURATION_DISK_ID,
    GLOW_COLOR_DISK_ID,
    GLOW_OPACITY_DISK_ID,
    GLOW_TYPE_DISK_ID,
    GLOW_GROUP_END_DISK_ID,
    GLOW_INDIVIDUAL_RADIUS_DISK_ID,
    GLOW_RADIUS_GROUP_START_DISK_ID,
    GLOW_RADIUS_X_DISK_ID,
    GLOW_RADIUS_Y_DISK_ID,
    GLOW_RADIUS_GROUP_END_DISK_ID,
};

// ─────────────────────────────────────────────────────────────────────────────
// Parameter ranges & defaults
// ─────────────────────────────────────────────────────────────────────────────

// Size (distance offset in pixels)
#define RIM_SIZE_MIN    0.0
#define RIM_SIZE_MAX    500.0
#define RIM_SIZE_DFLT   10.0

// Opacity (0–100 %)
#define RIM_OPACITY_MIN  0.0
#define RIM_OPACITY_MAX  100.0
#define RIM_OPACITY_DFLT 100.0

// Angle default: 180 degrees → cahaya dari atas → rim di atas layer
#define RIM_ANGLE_DFLT   180

// Softness (Gaussian blur radius on rim mask, in pixels)
#define RIM_SOFTNESS_MIN   0.0
#define RIM_SOFTNESS_MAX   100.0
#define RIM_SOFTNESS_DFLT  0.0

// Blend Mode choices
#define RIM_BLEND_NORMAL   0
#define RIM_BLEND_ADD      1
#define RIM_BLEND_SCREEN   2
#define RIM_BLEND_DFLT     RIM_BLEND_NORMAL

// ─────────────────────────────────────────────────────────────────────────────
// Glow parameters
// ─────────────────────────────────────────────────────────────────────────────

// Threshold: rim mask values below this are cut off
#define GLOW_THRESHOLD_MIN   0.0
#define GLOW_THRESHOLD_MAX   1.0
#define GLOW_THRESHOLD_DFLT  0.2

// Threshold Smooth: soft knee width around threshold
#define GLOW_THRESHOLD_SMOOTH_MIN   0.0
#define GLOW_THRESHOLD_SMOOTH_MAX   1.0
#define GLOW_THRESHOLD_SMOOTH_DFLT  0.0

// Radius: stage-1 (tight) blur radius. Stage-2 = Radius * GLOW_STAGE2_MULT.
#define GLOW_INDIVIDUAL_RADIUS_DFLT  1   // default: individual X/Y mode
#define GLOW_RADIUS_MIN   0.0
#define GLOW_RADIUS_MAX   200.0
#define GLOW_RADIUS_DFLT  5.25
#define GLOW_RADIUS_X_MIN   0.0
#define GLOW_RADIUS_X_MAX   200.0
#define GLOW_RADIUS_X_DFLT  5.0
#define GLOW_RADIUS_Y_MIN   0.0
#define GLOW_RADIUS_Y_MAX   200.0
#define GLOW_RADIUS_Y_DFLT  20.0
#define GLOW_STAGE2_MULT  6.0   // stage-2 radius = radius * 6
#define GLOW_INSIDE_ATTEN  0.35 // inside glow multiplier (outside = 1.0, inside = this)

// Intensity: brightness multiplier on glow map before composite
#define GLOW_INTENSITY_MIN   0.0
#define GLOW_INTENSITY_MAX   500.0
#define GLOW_INTENSITY_DFLT  0.0

// Saturation: 0 = fully desaturated to glow color, 1 = preserve source hue
#define GLOW_SATURATION_MIN   0.0
#define GLOW_SATURATION_MAX   1.0
#define GLOW_SATURATION_DFLT  0.0

// Opacity: final glow composite opacity (0–1)
#define GLOW_OPACITY_MIN   0.0
#define GLOW_OPACITY_MAX   1.0
#define GLOW_OPACITY_DFLT  1.0

// Glow Type popup choices (1-based in AE)
#define GLOW_TYPE_NORMAL   0
#define GLOW_TYPE_ADD      1
#define GLOW_TYPE_SCREEN   2   // default
#define GLOW_TYPE_DFLT     GLOW_TYPE_SCREEN

// ─────────────────────────────────────────────────────────────────────────────
// String IDs (must match RimLight_Strings.h / .cpp)
// ─────────────────────────────────────────────────────────────────────────────
enum {
    StrID_NONE,
    StrID_Name,
    StrID_Description,
    StrID_RimColor_Param_Name,
    StrID_RimSize_Param_Name,
    StrID_RimOpacity_Param_Name,
    StrID_RimAngle_Param_Name,
    StrID_RimSoftness_Param_Name,
    StrID_RimBlendMode_Param_Name,
    // Glow group
    StrID_GlowGroup_Name,
    StrID_GlowThreshold_Param_Name,
    StrID_GlowThresholdSmooth_Param_Name,
    StrID_GlowRadius_Param_Name,
    StrID_GlowIndividualRadius_Param_Name,
    StrID_GlowRadiusGroup_Name,
    StrID_GlowRadiusX_Param_Name,
    StrID_GlowRadiusY_Param_Name,
    StrID_GlowIntensity_Param_Name,
    StrID_GlowSaturation_Param_Name,
    StrID_GlowColor_Param_Name,
    StrID_GlowOpacity_Param_Name,
    StrID_GlowType_Param_Name,
    StrID_NUMTYPES
};

// ─────────────────────────────────────────────────────────────────────────────
// Info struct passed to render functions
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    PF_Pixel    rim_color;          // RGBA warna rim (red/green/blue 0–255)
    double      rim_size;           // offset distance dalam pixel
    double      rim_opacity;        // 0–100
    double      rim_angle;          // derajat (sudah dikonversi dari AE fixed-point)
    double      rim_softness;       // Gaussian blur radius pada rim mask (pixel)
    A_long      rim_blend_mode;     // 0=Normal, 1=Add, 2=Screen

    // ── Glow ────────────────────────────────────────────────────────────────
    double      glow_threshold;         // 0–1: cut-off pada rim mask
    double      glow_threshold_smooth;  // 0–1: soft knee width
    A_long      glow_individual;         // 1=individual X/Y mode, 0=uniform
    double      glow_radius;            // uniform radius (pixel, full-res)
    double      glow_radius_x;          // individual radius X (pixel, full-res)
    double      glow_radius_y;          // individual radius Y (pixel, full-res)
    double      glow_intensity;         // brightness multiplier (0–10)
    double      glow_saturation;        // 0=pure glow color, 1=preserve hue
    PF_Pixel    glow_color;             // tint warna glow
    double      glow_opacity;           // 0–1 final opacity
    A_long      glow_type;              // 0=Normal, 1=Add, 2=Screen
} RimLightInfo;

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {
    DllExport PF_Err EffectMain(
        PF_Cmd       cmd,
        PF_InData* in_data,
        PF_OutData* out_data,
        PF_ParamDef* params[],
        PF_LayerDef* output,
        void* extra);
}

#endif // RIMLIGHT_H