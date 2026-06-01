#include "RimLight.h"

static PF_Err About(
    PF_InData  *in_data,
    PF_OutData *out_data,
    PF_ParamDef *params[],
    PF_LayerDef *output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(
        out_data->return_msg,
        "%s v%d.%d\r%s",
        STR(StrID_Name),
        MAJOR_VERSION,
        MINOR_VERSION,
        STR(StrID_Description)
    );
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(
    PF_InData  *in_data,
    PF_OutData *out_data,
    PF_ParamDef *params[],
    PF_LayerDef *output)
{
    out_data->my_version = PF_VERSION(
        MAJOR_VERSION,
        MINOR_VERSION,
        BUG_VERSION,
        STAGE_VERSION,
        BUILD_VERSION
    );

    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_SEND_UPDATE_PARAMS_UI;

    out_data->out_flags2 =
        PF_OutFlag2_SUPPORTS_THREADED_RENDERING |
        PF_OutFlag2_AE13_5_THREADSAFE;

    return PF_Err_NONE;
}

static PF_Err ParamsSetup(
    PF_InData  *in_data,
    PF_OutData *out_data,
    PF_ParamDef *params[],
    PF_LayerDef *output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        STR(StrID_RimType_Param_Name),
        2,
        1,
        "Single|Both Sides",
        RIM_TYPE
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        STR(StrID_RimBlendMode_Param_Name),
        6,
        1,
        "Normal|Darken|Multiply|Color Burn|Overlay|Add",
        RIM_BLEND_MODE
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR(
        STR(StrID_RimColor_Param_Name),
        PF_MAX_CHAN8,
        PF_MAX_CHAN8,
        PF_MAX_CHAN8,
        RIM_COLOR
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_RimSize_Param_Name),
        RIM_SIZE_MIN,
        RIM_SIZE_MAX,
        RIM_SIZE_MIN,
        RIM_SIZE_MAX,
        RIM_SIZE_DFLT,
        PF_Precision_HUNDREDTHS,
        0,
        0,
        RIM_SIZE
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_RimIntensity_Param_Name),
        RIM_INTENSITY_MIN,
        RIM_INTENSITY_MAX,
        RIM_INTENSITY_MIN,
        RIM_INTENSITY_MAX,
        RIM_INTENSITY_DFLT,
        PF_Precision_HUNDREDTHS,
        0,
        0,
        RIM_INTENSITY
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_RimContrast_Param_Name),
        RIM_CONTRAST_MIN,
        RIM_CONTRAST_MAX,
        RIM_CONTRAST_MIN,
        RIM_CONTRAST_MAX,
        RIM_CONTRAST_DFLT,
        PF_Precision_HUNDREDTHS,
        0,
        0,
        RIM_CONTRAST
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE(
        STR(StrID_RimAngle_Param_Name),
        RIM_ANGLE_DFLT,
        RIM_ANGLE
    );

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_RimOpacity_Param_Name),
        RIM_OPACITY_MIN,
        RIM_OPACITY_MAX,
        RIM_OPACITY_MIN,
        RIM_OPACITY_MAX,
        RIM_OPACITY_DFLT,
        PF_Precision_HUNDREDTHS,
        0,
        0,
        RIM_OPACITY
    );

    out_data->num_params = RIM_NUM_PARAMS;
    return err;
}

template <typename PIX>
static inline A_u_char clamp255(int v) {
    return (A_u_char)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline A_u_short clamp65535(int v) {
    return (A_u_short)(v < 0 ? 0 : (v > 32768 ? 32768 : v));
}

static PF_Err RimRender8(
    PF_InData *in_data,
    PF_LayerDef *src,
    PF_LayerDef *dst,
    const RimLightInfo &info)
{
    const A_long width = dst->width;
    const A_long height = dst->height;
    const PF_Pixel8 *src_base = reinterpret_cast<const PF_Pixel8*>(src->data);
    PF_Pixel8 *dst_base = reinterpret_cast<PF_Pixel8*>(dst->data);

    const double angle_rad = info.rim_angle * 3.14159265358979323846 / 180.0;
    const double lx = cos(angle_rad);
    const double ly = sin(angle_rad);
    const double size = info.rim_size < 0.0 ? 0.0 : info.rim_size;
    const double intensity = info.rim_intensity / 100.0;
    const double contrast = info.rim_contrast / 100.0;
    const double opacity = info.rim_opacity / 100.0;
    const int radius = (int)(size + 0.5);

    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel8 &sp = src_base[y * src->rowbytes / sizeof(PF_Pixel8) + x];
            PF_Pixel8 &dp = dst_base[y * dst->rowbytes / sizeof(PF_Pixel8) + x];

            int minA = 255;
            for (int oy = -radius; oy <= radius; ++oy) {
                const int sy = y + oy;
                if (sy < 0 || sy >= height) continue;
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = x + ox;
                    if (sx < 0 || sx >= width) continue;
                    const PF_Pixel8 &p = src_base[sy * src->rowbytes / sizeof(PF_Pixel8) + sx];
                    if (p.alpha < minA) minA = p.alpha;
                }
            }

            const double edge = (double)sp.alpha - (double)minA;
            if (edge <= 0.0) {
                dp = sp;
                continue;
            }

            // Sobel-like local gradient on alpha for directionality
            auto alphaAt = [&](int px, int py) -> double {
                if (px < 0 || px >= width || py < 0 || py >= height) return (double)sp.alpha;
                return (double)src_base[py * src->rowbytes / sizeof(PF_Pixel8) + px].alpha;
            };

            const double gx =
                -alphaAt((int)x - 1, (int)y - 1) - 2.0 * alphaAt((int)x - 1, (int)y) - alphaAt((int)x - 1, (int)y + 1) +
                 alphaAt((int)x + 1, (int)y - 1) + 2.0 * alphaAt((int)x + 1, (int)y) + alphaAt((int)x + 1, (int)y + 1);
            const double gy =
                -alphaAt((int)x - 1, (int)y - 1) - 2.0 * alphaAt((int)x, (int)y - 1) - alphaAt((int)x + 1, (int)y - 1) +
                 alphaAt((int)x - 1, (int)y + 1) + 2.0 * alphaAt((int)x, (int)y + 1) + alphaAt((int)x + 1, (int)y + 1);

            double len = sqrt(gx * gx + gy * gy);
            double nx = 0.0, ny = 0.0;
            if (len > 1e-6) {
                nx = gx / len;
                ny = gy / len;
            }

            double dot = nx * lx + ny * ly;
            if (info.rim_type == RIM_TYPE_SINGLE) {
                if (dot < 0.0) dot = 0.0;
            } else {
                dot = fabs(dot);
            }

            double mask = (edge / 255.0) * dot * intensity;
            if (mask < 0.0) mask = 0.0;
            if (mask > 1.0) mask = 1.0;
            if (contrast > 0.0) {
                mask = pow(mask, 1.0 / (1.0 - contrast + 0.001));
            }

            const double rimA = mask * opacity;
            const double rimR = ((double)info.rim_color.red / 255.0) * 255.0 * rimA;
            const double rimG = ((double)info.rim_color.green / 255.0) * 255.0 * rimA;
            const double rimB = ((double)info.rim_color.blue / 255.0) * 255.0 * rimA;

            // Normal/additive style blend
            if (info.rim_blend_mode == RIM_BLEND_ADD) {
                dp.red   = clamp255((int)sp.red   + (int)rimR);
                dp.green = clamp255((int)sp.green + (int)rimG);
                dp.blue  = clamp255((int)sp.blue  + (int)rimB);
                dp.alpha = sp.alpha;
            } else {
                // simple over composite
                const double inv = 1.0 - rimA;
                dp.red   = clamp255((int)(sp.red   * inv + rimR));
                dp.green = clamp255((int)(sp.green * inv + rimG));
                dp.blue  = clamp255((int)(sp.blue  * inv + rimB));
                dp.alpha = sp.alpha;
            }
        }
    }

    return PF_Err_NONE;
}

static PF_Err RimRender16(
    PF_InData *in_data,
    PF_LayerDef *src,
    PF_LayerDef *dst,
    const RimLightInfo &info)
{
    const A_long width = dst->width;
    const A_long height = dst->height;
    const PF_Pixel16 *src_base = reinterpret_cast<const PF_Pixel16*>(src->data);
    PF_Pixel16 *dst_base = reinterpret_cast<PF_Pixel16*>(dst->data);

    const double angle_rad = info.rim_angle * 3.14159265358979323846 / 180.0;
    const double lx = cos(angle_rad);
    const double ly = sin(angle_rad);
    const double size = info.rim_size < 0.0 ? 0.0 : info.rim_size;
    const double intensity = info.rim_intensity / 100.0;
    const double contrast = info.rim_contrast / 100.0;
    const double opacity = info.rim_opacity / 100.0;
    const int radius = (int)(size + 0.5);

    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel16 &sp = src_base[y * src->rowbytes / sizeof(PF_Pixel16) + x];
            PF_Pixel16 &dp = dst_base[y * dst->rowbytes / sizeof(PF_Pixel16) + x];

            int minA = 32768;
            for (int oy = -radius; oy <= radius; ++oy) {
                const int sy = y + oy;
                if (sy < 0 || sy >= height) continue;
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = x + ox;
                    if (sx < 0 || sx >= width) continue;
                    const PF_Pixel16 &p = src_base[sy * src->rowbytes / sizeof(PF_Pixel16) + sx];
                    if (p.alpha < minA) minA = p.alpha;
                }
            }

            const double edge = (double)sp.alpha - (double)minA;
            if (edge <= 0.0) {
                dp = sp;
                continue;
            }

            auto alphaAt = [&](int px, int py) -> double {
                if (px < 0 || px >= width || py < 0 || py >= height) return (double)sp.alpha;
                return (double)src_base[py * src->rowbytes / sizeof(PF_Pixel16) + px].alpha;
            };

            const double gx =
                -alphaAt((int)x - 1, (int)y - 1) - 2.0 * alphaAt((int)x - 1, (int)y) - alphaAt((int)x - 1, (int)y + 1) +
                 alphaAt((int)x + 1, (int)y - 1) + 2.0 * alphaAt((int)x + 1, (int)y) + alphaAt((int)x + 1, (int)y + 1);
            const double gy =
                -alphaAt((int)x - 1, (int)y - 1) - 2.0 * alphaAt((int)x, (int)y - 1) - alphaAt((int)x + 1, (int)y - 1) +
                 alphaAt((int)x - 1, (int)y + 1) + 2.0 * alphaAt((int)x, (int)y + 1) + alphaAt((int)x + 1, (int)y + 1);

            double len = sqrt(gx * gx + gy * gy);
            double nx = 0.0, ny = 0.0;
            if (len > 1e-6) {
                nx = gx / len;
                ny = gy / len;
            }

            double dot = nx * lx + ny * ly;
            if (info.rim_type == RIM_TYPE_SINGLE) {
                if (dot < 0.0) dot = 0.0;
            } else {
                dot = fabs(dot);
            }

            double mask = (edge / 32768.0) * dot * intensity;
            if (mask < 0.0) mask = 0.0;
            if (mask > 1.0) mask = 1.0;
            if (contrast > 0.0) {
                mask = pow(mask, 1.0 / (1.0 - contrast + 0.001));
            }

            const double rimA = mask * opacity;
            const double rimR = ((double)info.rim_color.red / 255.0) * 32768.0 * rimA;
            const double rimG = ((double)info.rim_color.green / 255.0) * 32768.0 * rimA;
            const double rimB = ((double)info.rim_color.blue / 255.0) * 32768.0 * rimA;

            if (info.rim_blend_mode == RIM_BLEND_ADD) {
                dp.red   = clamp65535((int)sp.red   + (int)rimR);
                dp.green = clamp65535((int)sp.green + (int)rimG);
                dp.blue  = clamp65535((int)sp.blue  + (int)rimB);
                dp.alpha = sp.alpha;
            } else {
                const double inv = 1.0 - rimA;
                dp.red   = clamp65535((int)(sp.red   * inv + rimR));
                dp.green = clamp65535((int)(sp.green * inv + rimG));
                dp.blue  = clamp65535((int)(sp.blue  * inv + rimB));
                dp.alpha = sp.alpha;
            }
        }
    }

    return PF_Err_NONE;
}

static PF_Err Render(
    PF_InData  *in_data,
    PF_OutData *out_data,
    PF_ParamDef *params[],
    PF_LayerDef *output)
{
    PF_Err err = PF_Err_NONE;
    PF_LayerDef *input = &params[RIM_INPUT]->u.ld;

    RimLightInfo info;
    AEFX_CLR_STRUCT(info);
    info.rim_type       = params[RIM_TYPE]->u.pd.value;
    info.rim_blend_mode = params[RIM_BLEND_MODE]->u.pd.value;
    info.rim_color      = params[RIM_COLOR]->u.cd.value;
    info.rim_size       = params[RIM_SIZE]->u.fs_d.value;
    info.rim_intensity  = params[RIM_INTENSITY]->u.fs_d.value;
    info.rim_contrast   = params[RIM_CONTRAST]->u.fs_d.value;
    info.rim_angle      = params[RIM_ANGLE]->u.ad.value;
    info.rim_opacity    = params[RIM_OPACITY]->u.fs_d.value;

    if (PF_WORLD_IS_DEEP(output)) {
        err = RimRender16(in_data, input, output, info);
    } else {
        err = RimRender8(in_data, input, output, info);
    }

    return err;
}

extern "C" DllExport PF_Err EffectMain(
    PF_Cmd      cmd,
    PF_InData  *in_data,
    PF_OutData *out_data,
    PF_ParamDef *params[],
    PF_LayerDef *output,
    void       *extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(in_data, out_data, params, output);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_RENDER:
                err = Render(in_data, out_data, params, output);
                break;
            default:
                break;
        }
    } catch (PF_Err &thrown) {
        err = thrown;
    }
    return err;
}
