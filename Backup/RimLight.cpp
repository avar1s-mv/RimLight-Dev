#include "RimLight.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline A_u_char clamp255(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return static_cast<A_u_char>(v);
}

static inline A_u_short clamp32768(int v)
{
    if (v < 0)     return 0;
    if (v > 32768) return 32768;
    return static_cast<A_u_short>(v);
}

// ─────────────────────────────────────────────────────────────────────────────
// About
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err About(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(
        out_data->return_msg,
        "%s v%d.%d\r%s",
        STR(StrID_Name),
        MAJOR_VERSION,
        MINOR_VERSION,
        STR(StrID_Description));
    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// GlobalSetup
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err GlobalSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    out_data->my_version = 524289;

    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_SEND_UPDATE_PARAMS_UI;

    out_data->out_flags2 = 0;

    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// ParamsSetup
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err ParamsSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err      err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(STR(StrID_RimType_Param_Name), 2, 1, "Single|Both Sides", RIM_TYPE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(STR(StrID_RimBlendMode_Param_Name), 2, 1, "Normal|Add", RIM_BLEND_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR(STR(StrID_RimColor_Param_Name), PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8, RIM_COLOR);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimSize_Param_Name),
        RIM_SIZE_MIN, RIM_SIZE_MAX, RIM_SIZE_MIN, RIM_SIZE_MAX, RIM_SIZE_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_SIZE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimIntensity_Param_Name),
        RIM_INTENSITY_MIN, RIM_INTENSITY_MAX, RIM_INTENSITY_MIN, RIM_INTENSITY_MAX, RIM_INTENSITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_INTENSITY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimContrast_Param_Name),
        RIM_CONTRAST_MIN, RIM_CONTRAST_MAX, RIM_CONTRAST_MIN, RIM_CONTRAST_MAX, RIM_CONTRAST_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_CONTRAST);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE(STR(StrID_RimAngle_Param_Name), RIM_ANGLE_DFLT, RIM_ANGLE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimOpacity_Param_Name),
        RIM_OPACITY_MIN, RIM_OPACITY_MAX, RIM_OPACITY_MIN, RIM_OPACITY_MAX, RIM_OPACITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_OPACITY);

    // ── Darken Group ──────────────────────────────────────────────────────
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_GROUP_START;
    def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    def.uu.id = DARKEN_DISK_ID;
    PF_STRCPY(def.name, STR(StrID_Darken_Group_Name));
    PF_ADD_PARAM(in_data, -1, &def);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_DarkenAmount_Param_Name),
        DARKEN_AMOUNT_MIN, DARKEN_AMOUNT_MAX, DARKEN_AMOUNT_MIN, DARKEN_AMOUNT_MAX, DARKEN_AMOUNT_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, DARKEN_AMOUNT);

    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_GROUP_END;
    def.flags = 0;
    def.uu.id = DARKEN_GROUP_END;
    PF_ADD_PARAM(in_data, -1, &def);

    // ── Glow Group ────────────────────────────────────────────────────────
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_GROUP_START;
    def.flags = PF_ParamFlag_COLLAPSE_TWIRLY;
    def.uu.id = GLOW_DISK_ID;
    PF_STRCPY(def.name, STR(StrID_Glow_Group_Name));
    PF_ADD_PARAM(in_data, -1, &def);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowThreshold_Param_Name),
        GLOW_THRESHOLD_MIN, GLOW_THRESHOLD_MAX, GLOW_THRESHOLD_MIN, GLOW_THRESHOLD_MAX, GLOW_THRESHOLD_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_THRESHOLD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowRadius_Param_Name),
        GLOW_RADIUS_MIN, GLOW_RADIUS_MAX, GLOW_RADIUS_MIN, GLOW_RADIUS_MAX, GLOW_RADIUS_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_RADIUS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowIntensity_Param_Name),
        GLOW_INTENSITY_MIN, GLOW_INTENSITY_MAX, GLOW_INTENSITY_MIN, GLOW_INTENSITY_MAX, GLOW_INTENSITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_INTENSITY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR(STR(StrID_GlowColor_Param_Name), PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8, GLOW_COLOR);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowOpacity_Param_Name),
        GLOW_OPACITY_MIN, GLOW_OPACITY_MAX, GLOW_OPACITY_MIN, GLOW_OPACITY_MAX, GLOW_OPACITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_OPACITY);

    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_GROUP_END;
    def.flags = 0;
    def.uu.id = GLOW_GROUP_END;
    PF_ADD_PARAM(in_data, -1, &def);

    out_data->num_params = RIM_NUM_PARAMS;
    return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pre-compute erode map: for each pixel, store minimum alpha in radius
// This is O(W*H*R^2) done ONCE, not per-pixel nested
// ─────────────────────────────────────────────────────────────────────────────

static void BuildErodeMap8(
    const PF_Pixel8* src, A_long src_stride,
    float* erode_map,
    A_long width, A_long height, int radius)
{
    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            int minA = 255;
            for (int oy = -radius; oy <= radius; ++oy) {
                const int sy = y + oy;
                if (sy < 0 || sy >= height) continue;
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = x + ox;
                    if (sx < 0 || sx >= width) continue;
                    int a = src[sy * src_stride + sx].alpha;
                    if (a < minA) minA = a;
                }
            }
            erode_map[y * width + x] = (float)minA / 255.0f;
        }
    }
}

static void BuildErodeMap16(
    const PF_Pixel16* src, A_long src_stride,
    float* erode_map,
    A_long width, A_long height, int radius)
{
    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            int minA = 32768;
            for (int oy = -radius; oy <= radius; ++oy) {
                const int sy = y + oy;
                if (sy < 0 || sy >= height) continue;
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = x + ox;
                    if (sx < 0 || sx >= width) continue;
                    int a = src[sy * src_stride + sx].alpha;
                    if (a < minA) minA = a;
                }
            }
            erode_map[y * width + x] = (float)minA / 32768.0f;
        }
    }
}

// Box blur on erode map for glow spread — O(W*H*R^2) once
static void BoxBlurMap(
    const float* src_map, float* dst_map,
    A_long width, A_long height, int radius)
{
    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            float sum = 0.0f;
            int   cnt = 0;
            for (int oy = -radius; oy <= radius; ++oy) {
                const int sy = y + oy;
                if (sy < 0 || sy >= height) continue;
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = x + ox;
                    if (sx < 0 || sx >= width) continue;
                    sum += src_map[sy * width + sx];
                    ++cnt;
                }
            }
            dst_map[y * width + x] = cnt > 0 ? sum / cnt : 0.0f;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render 8-bit
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err RimRender8(
    PF_InData* in_data,
    PF_LayerDef* src,
    PF_LayerDef* dst,
    const RimLightInfo& info)
{
    const A_long width = dst->width;
    const A_long height = dst->height;
    const A_long src_stride = src->rowbytes / sizeof(PF_Pixel8);
    const A_long dst_stride = dst->rowbytes / sizeof(PF_Pixel8);

    const PF_Pixel8* src_base = reinterpret_cast<const PF_Pixel8*>(src->data);
    PF_Pixel8* dst_base = reinterpret_cast<PF_Pixel8*>(dst->data);

    const double angle_rad = info.rim_angle * 3.14159265358979323846 / 180.0;
    const double lx = cos(angle_rad);
    const double ly = sin(angle_rad);
    const double intensity = info.rim_intensity / 100.0;
    const double contrast = info.rim_contrast / 100.0;
    const double opacity = info.rim_opacity / 100.0;
    const double darken = info.darken_amount / 100.0;
    const int    radius = (int)(info.rim_size + 0.5);

    const double glow_thresh = info.glow_threshold / 100.0;
    const int    glow_radius = (int)(info.glow_radius + 0.5);
    const double glow_intens = info.glow_intensity / 100.0;
    const double glow_opacity = info.glow_opacity / 100.0;

    // ── Pre-compute erode map once ────────────────────────────────────────
    float* erode_map = new float[width * height];
    BuildErodeMap8(src_base, src_stride, erode_map, width, height, radius);

    // ── Pre-compute glow map (box blur of rim edge map) ───────────────────
    float* glow_map = nullptr;
    if (glow_intens > 0.0 && glow_radius > 0) {
        // First build edge map (alpha - eroded alpha)
        float* edge_map = new float[width * height];
        for (A_long i = 0; i < width* height; ++i) {
            float srcAlpha = src_base[(i / width) * src_stride + (i % width)].alpha / 255.0f;
            float e = srcAlpha - erode_map[i];
            edge_map[i] = e > 0.0f ? e : 0.0f;
        }
        glow_map = new float[width * height];
        BoxBlurMap(edge_map, glow_map, width, height, glow_radius);
        delete[] edge_map;
    }

    // ── Per-pixel render ──────────────────────────────────────────────────
    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel8& sp = src_base[y * src_stride + x];
            PF_Pixel8& dp = dst_base[y * dst_stride + x];

            const float  eroded = erode_map[y * width + x];
            const double srcAlpha = sp.alpha / 255.0;
            const double edge = srcAlpha - (double)eroded;

            // ── Darken ────────────────────────────────────────────────────
            double baseR = sp.red * (1.0 - darken * srcAlpha);
            double baseG = sp.green * (1.0 - darken * srcAlpha);
            double baseB = sp.blue * (1.0 - darken * srcAlpha);

            // ── Rim mask with Sobel direction ─────────────────────────────
            double rimA = 0.0;
            if (edge > 0.0) {
                auto alphaAt = [&](int px, int py) -> double {
                    if (px < 0 || px >= width || py < 0 || py >= height)
                        return srcAlpha;
                    return src_base[py * src_stride + px].alpha / 255.0;
                    };

                const double gx =
                    -alphaAt(x - 1, y - 1) - 2.0 * alphaAt(x - 1, y) - alphaAt(x - 1, y + 1) +
                    alphaAt(x + 1, y - 1) + 2.0 * alphaAt(x + 1, y) + alphaAt(x + 1, y + 1);
                const double gy =
                    -alphaAt(x - 1, y - 1) - 2.0 * alphaAt(x, y - 1) - alphaAt(x + 1, y - 1) +
                    alphaAt(x - 1, y + 1) + 2.0 * alphaAt(x, y + 1) + alphaAt(x + 1, y + 1);

                double len = sqrt(gx * gx + gy * gy);
                double nx = 0.0, ny = 0.0;
                if (len > 1e-6) { nx = gx / len; ny = gy / len; }

                double dot = nx * lx + ny * ly;
                if (info.rim_type == RIM_TYPE_SINGLE) {
                    if (dot < 0.0) dot = 0.0;
                }
                else {
                    dot = fabs(dot);
                }

                // edge already encodes rim width via erode depth
                double mask = edge * dot * intensity;
                if (mask < 0.0) mask = 0.0;
                if (mask > 1.0) mask = 1.0;
                if (contrast > 0.0)
                    mask = pow(mask, 1.0 / (1.0 - contrast + 0.001));

                rimA = mask * opacity;
            }

            const double rimR = (info.rim_color.red / 255.0) * 255.0 * rimA;
            const double rimG = (info.rim_color.green / 255.0) * 255.0 * rimA;
            const double rimB = (info.rim_color.blue / 255.0) * 255.0 * rimA;

            // ── Blend rim ─────────────────────────────────────────────────
            double outR, outG, outB;
            if (info.rim_blend_mode == RIM_BLEND_ADD) {
                outR = baseR + rimR;
                outG = baseG + rimG;
                outB = baseB + rimB;
            }
            else {
                double inv = 1.0 - rimA;
                outR = baseR * inv + rimR;
                outG = baseG * inv + rimG;
                outB = baseB * inv + rimB;
            }

            // ── Glow ──────────────────────────────────────────────────────
            if (glow_map && glow_intens > 0.0) {
                double glowMask = (double)glow_map[y * width + x];
                if (glowMask > glow_thresh) {
                    glowMask = (glowMask - glow_thresh) / (1.0 - glow_thresh + 0.001);
                    glowMask *= glow_intens * glow_opacity;
                    if (glowMask > 1.0) glowMask = 1.0;

                    outR += (info.glow_color.red / 255.0) * 255.0 * glowMask;
                    outG += (info.glow_color.green / 255.0) * 255.0 * glowMask;
                    outB += (info.glow_color.blue / 255.0) * 255.0 * glowMask;
                }
            }

            dp.red = clamp255((int)outR);
            dp.green = clamp255((int)outG);
            dp.blue = clamp255((int)outB);
            dp.alpha = sp.alpha;
        }
    }

    delete[] erode_map;
    if (glow_map) delete[] glow_map;

    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render 16-bit
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err RimRender16(
    PF_InData* in_data,
    PF_LayerDef* src,
    PF_LayerDef* dst,
    const RimLightInfo& info)
{
    const A_long width = dst->width;
    const A_long height = dst->height;
    const A_long src_stride = src->rowbytes / sizeof(PF_Pixel16);
    const A_long dst_stride = dst->rowbytes / sizeof(PF_Pixel16);

    const PF_Pixel16* src_base = reinterpret_cast<const PF_Pixel16*>(src->data);
    PF_Pixel16* dst_base = reinterpret_cast<PF_Pixel16*>(dst->data);

    const double angle_rad = info.rim_angle * 3.14159265358979323846 / 180.0;
    const double lx = cos(angle_rad);
    const double ly = sin(angle_rad);
    const double intensity = info.rim_intensity / 100.0;
    const double contrast = info.rim_contrast / 100.0;
    const double opacity = info.rim_opacity / 100.0;
    const double darken = info.darken_amount / 100.0;
    const int    radius = (int)(info.rim_size + 0.5);

    const double glow_thresh = info.glow_threshold / 100.0;
    const int    glow_radius = (int)(info.glow_radius + 0.5);
    const double glow_intens = info.glow_intensity / 100.0;
    const double glow_opacity = info.glow_opacity / 100.0;

    float* erode_map = new float[width * height];
    BuildErodeMap16(src_base, src_stride, erode_map, width, height, radius);

    float* glow_map = nullptr;
    if (glow_intens > 0.0 && glow_radius > 0) {
        float* edge_map = new float[width * height];
        for (A_long y = 0; y < height; ++y) {
            for (A_long x = 0; x < width; ++x) {
                float srcAlpha = src_base[y * src_stride + x].alpha / 32768.0f;
                float e = srcAlpha - erode_map[y * width + x];
                edge_map[y * width + x] = e > 0.0f ? e : 0.0f;
            }
        }
        glow_map = new float[width * height];
        BoxBlurMap(edge_map, glow_map, width, height, glow_radius);
        delete[] edge_map;
    }

    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel16& sp = src_base[y * src_stride + x];
            PF_Pixel16& dp = dst_base[y * dst_stride + x];

            const float  eroded = erode_map[y * width + x];
            const double srcAlpha = sp.alpha / 32768.0;
            const double edge = srcAlpha - (double)eroded;

            double baseR = sp.red * (1.0 - darken * srcAlpha);
            double baseG = sp.green * (1.0 - darken * srcAlpha);
            double baseB = sp.blue * (1.0 - darken * srcAlpha);

            double rimA = 0.0;
            if (edge > 0.0) {
                auto alphaAt = [&](int px, int py) -> double {
                    if (px < 0 || px >= width || py < 0 || py >= height)
                        return srcAlpha;
                    return src_base[py * src_stride + px].alpha / 32768.0;
                    };

                const double gx =
                    -alphaAt(x - 1, y - 1) - 2.0 * alphaAt(x - 1, y) - alphaAt(x - 1, y + 1) +
                    alphaAt(x + 1, y - 1) + 2.0 * alphaAt(x + 1, y) + alphaAt(x + 1, y + 1);
                const double gy =
                    -alphaAt(x - 1, y - 1) - 2.0 * alphaAt(x, y - 1) - alphaAt(x + 1, y - 1) +
                    alphaAt(x - 1, y + 1) + 2.0 * alphaAt(x, y + 1) + alphaAt(x + 1, y + 1);

                double len = sqrt(gx * gx + gy * gy);
                double nx = 0.0, ny = 0.0;
                if (len > 1e-6) { nx = gx / len; ny = gy / len; }

                double dot = nx * lx + ny * ly;
                if (info.rim_type == RIM_TYPE_SINGLE) {
                    if (dot < 0.0) dot = 0.0;
                }
                else {
                    dot = fabs(dot);
                }

                double mask = edge * dot * intensity;
                if (mask < 0.0) mask = 0.0;
                if (mask > 1.0) mask = 1.0;
                if (contrast > 0.0)
                    mask = pow(mask, 1.0 / (1.0 - contrast + 0.001));

                rimA = mask * opacity;
            }

            const double rimR = (info.rim_color.red / 255.0) * 32768.0 * rimA;
            const double rimG = (info.rim_color.green / 255.0) * 32768.0 * rimA;
            const double rimB = (info.rim_color.blue / 255.0) * 32768.0 * rimA;

            double outR, outG, outB;
            if (info.rim_blend_mode == RIM_BLEND_ADD) {
                outR = baseR + rimR;
                outG = baseG + rimG;
                outB = baseB + rimB;
            }
            else {
                double inv = 1.0 - rimA;
                outR = baseR * inv + rimR;
                outG = baseG * inv + rimG;
                outB = baseB * inv + rimB;
            }

            if (glow_map && glow_intens > 0.0) {
                double glowMask = (double)glow_map[y * width + x];
                if (glowMask > glow_thresh) {
                    glowMask = (glowMask - glow_thresh) / (1.0 - glow_thresh + 0.001);
                    glowMask *= glow_intens * glow_opacity;
                    if (glowMask > 1.0) glowMask = 1.0;

                    outR += (info.glow_color.red / 255.0) * 32768.0 * glowMask;
                    outG += (info.glow_color.green / 255.0) * 32768.0 * glowMask;
                    outB += (info.glow_color.blue / 255.0) * 32768.0 * glowMask;
                }
            }

            dp.red = clamp32768((int)outR);
            dp.green = clamp32768((int)outG);
            dp.blue = clamp32768((int)outB);
            dp.alpha = sp.alpha;
        }
    }

    delete[] erode_map;
    if (glow_map) delete[] glow_map;

    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render dispatcher
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err Render(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err       err = PF_Err_NONE;
    PF_LayerDef* input = &params[RIM_INPUT]->u.ld;

    RimLightInfo info;
    AEFX_CLR_STRUCT(info);

    info.rim_type = params[RIM_TYPE]->u.pd.value;
    info.rim_blend_mode = params[RIM_BLEND_MODE]->u.pd.value;
    info.rim_color = params[RIM_COLOR]->u.cd.value;
    info.rim_size = params[RIM_SIZE]->u.fs_d.value;
    info.rim_intensity = params[RIM_INTENSITY]->u.fs_d.value;
    info.rim_contrast = params[RIM_CONTRAST]->u.fs_d.value;
    info.rim_angle = params[RIM_ANGLE]->u.ad.value;
    info.rim_opacity = params[RIM_OPACITY]->u.fs_d.value;
    info.darken_amount = params[DARKEN_AMOUNT]->u.fs_d.value;
    info.glow_threshold = params[GLOW_THRESHOLD]->u.fs_d.value;
    info.glow_radius = params[GLOW_RADIUS]->u.fs_d.value;
    info.glow_intensity = params[GLOW_INTENSITY]->u.fs_d.value;
    info.glow_color = params[GLOW_COLOR]->u.cd.value;
    info.glow_opacity = params[GLOW_OPACITY]->u.fs_d.value;

    if (PF_WORLD_IS_DEEP(output)) {
        err = RimRender16(in_data, input, output, info);
    }
    else {
        err = RimRender8(in_data, input, output, info);
    }

    return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// PreRender
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err PreRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_PreRenderExtra* extra)
{
    extra->output->result_rect = extra->input->output_request.rect;
    extra->output->max_result_rect = extra->input->output_request.rect;
    extra->output->solid = FALSE;
    extra->output->pre_render_data = NULL;
    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// EffectMain
// ─────────────────────────────────────────────────────────────────────────────

extern "C" DllExport PF_Err EffectMain(
    PF_Cmd       cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
        case PF_Cmd_ABOUT:
            err = About(in_data, out_data, params, output);       break;
        case PF_Cmd_GLOBAL_SETUP:
            err = GlobalSetup(in_data, out_data, params, output); break;
        case PF_Cmd_PARAMS_SETUP:
            err = ParamsSetup(in_data, out_data, params, output); break;
        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, output);      break;
        case PF_Cmd_SMART_PRE_RENDER:
            err = PreRender(in_data, out_data,
                reinterpret_cast<PF_PreRenderExtra*>(extra));     break;
        default: break;
        }
    }
    catch (PF_Err& thrown) { err = thrown; }
    return err;
}