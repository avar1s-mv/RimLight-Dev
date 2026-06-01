#include "RimLight.h"

typedef struct {
    A_u_long index;
    A_char   str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    { StrID_NONE, "" },
    { StrID_Name, "RimLight" },
    { StrID_Description, "Directional rim light effect for After Effects 2020." },
    { StrID_RimType_Param_Name, "Rim Type" },
    { StrID_RimBlendMode_Param_Name, "Blend Mode" },
    { StrID_RimColor_Param_Name, "Color" },
    { StrID_RimSize_Param_Name, "Size" },
    { StrID_RimIntensity_Param_Name, "Intensity" },
    { StrID_RimContrast_Param_Name, "Contrast" },
    { StrID_RimAngle_Param_Name, "Angle" },
    { StrID_RimOpacity_Param_Name, "Opacity" },
};

char *GetStringPtr(int strNum)
{
    return g_strs[strNum].str;
}
