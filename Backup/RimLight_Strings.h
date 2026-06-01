#pragma once

#ifndef RIMLIGHT_STRINGS_H
#define RIMLIGHT_STRINGS_H

typedef enum {
    StrID_NONE,
    StrID_Name,
    StrID_Description,

    // Rim
    StrID_RimType_Param_Name,
    StrID_RimBlendMode_Param_Name,
    StrID_RimColor_Param_Name,
    StrID_RimSize_Param_Name,
    StrID_RimIntensity_Param_Name,
    StrID_RimContrast_Param_Name,
    StrID_RimAngle_Param_Name,
    StrID_RimOpacity_Param_Name,

    // Darken group
    StrID_Darken_Group_Name,
    StrID_DarkenAmount_Param_Name,

    // Glow group
    StrID_Glow_Group_Name,
    StrID_GlowThreshold_Param_Name,
    StrID_GlowRadius_Param_Name,
    StrID_GlowIntensity_Param_Name,
    StrID_GlowColor_Param_Name,
    StrID_GlowOpacity_Param_Name,

    StrID_NUMTYPES
} StrIDType;

#endif // RIMLIGHT_STRINGS_H