#include "RimLight.h"

typedef struct {
    A_u_long index;
    A_char   str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    { StrID_NONE,                           "" },
    { StrID_Name,                           "RimLight" },
    { StrID_Description,                    "Directional rim light using offset-fill alpha matte." },

    { StrID_RimColor_Param_Name,            "Color" },
    { StrID_RimSize_Param_Name,             "Size" },
    { StrID_RimOpacity_Param_Name,          "Opacity" },
    { StrID_RimIntensity_Param_Name,        "Intensity" },
    { StrID_RimAngle_Param_Name,            "Angle" },
    { StrID_RimSoftness_Param_Name,         "Softness" },
    { StrID_RimBlendMode_Param_Name,        "Blend Mode" },

    { StrID_GlowGroup_Name,                 "Glow" },
    { StrID_GlowThreshold_Param_Name,       "Threshold" },
    { StrID_GlowThresholdSmooth_Param_Name, "Threshold Smooth" },
    { StrID_GlowRadius_Param_Name,          "Radius" },
    { StrID_GlowIndividualRadius_Param_Name,"Individual Radius" },
    { StrID_GlowRadiusGroup_Name,           "Separate Radius" },
    { StrID_GlowRadiusX_Param_Name,         "Radius X" },
    { StrID_GlowRadiusY_Param_Name,         "Radius Y" },
    { StrID_GlowIntensity_Param_Name,       "Intensity" },
    { StrID_GlowSaturation_Param_Name,      "Saturation" },
    { StrID_GlowColor_Param_Name,           "Color" },
    { StrID_GlowOpacity_Param_Name,         "Opacity" },
    { StrID_GlowType_Param_Name,            "Glow Type" },
};

char* GetStringPtr(int strNum)
{
    return g_strs[strNum].str;
}