#include "RimLight.h"

typedef struct {
    A_u_long index;
    A_char   str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    { StrID_NONE,                   "" },
    { StrID_Name,                   "RimLight" },
    { StrID_Description,            "Directional rim light effect with darkening and glow." },

    // Rim
    { StrID_RimType_Param_Name,         "Type" },
    { StrID_RimBlendMode_Param_Name,    "Blending Mode" },
    { StrID_RimColor_Param_Name,        "Color" },
    { StrID_RimSize_Param_Name,         "Size" },
    { StrID_RimIntensity_Param_Name,    "Intensity" },
    { StrID_RimContrast_Param_Name,     "Contrast" },
    { StrID_RimAngle_Param_Name,        "Angle" },
    { StrID_RimOpacity_Param_Name,      "Opacity" },

    // Darken
    { StrID_Darken_Group_Name,          "Darken" },
    { StrID_DarkenAmount_Param_Name,    "Amount" },

    // Glow
    { StrID_Glow_Group_Name,            "Glow" },
    { StrID_GlowThreshold_Param_Name,   "Threshold" },
    { StrID_GlowRadius_Param_Name,      "Radius" },
    { StrID_GlowIntensity_Param_Name,   "Intensity" },
    { StrID_GlowColor_Param_Name,       "Color" },
    { StrID_GlowOpacity_Param_Name,     "Opacity" },
};

char* GetStringPtr(int strNum)
{
    return g_strs[strNum].str;
}