#pragma once

#ifndef RIMLIGHT_STRINGS_H
#define RIMLIGHT_STRINGS_H

typedef enum {
    StrID_NONE,
    StrID_Name,
    StrID_Description,

    StrID_RimColor_Param_Name,
    StrID_RimSize_Param_Name,
    StrID_RimOpacity_Param_Name,
    StrID_RimIntensity_Param_Name,
    StrID_RimAngle_Param_Name,
    StrID_RimSoftness_Param_Name,
    StrID_RimBlendMode_Param_Name,

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
} StrIDType;

#endif // RIMLIGHT_STRINGS_H