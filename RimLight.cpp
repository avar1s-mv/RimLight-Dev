#include "RimLight.h"
#include <new>
#include <cstring>

#ifndef PF_ParamFlag_INVISIBLE
#define PF_ParamFlag_INVISIBLE (1L << 19)
#endif

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
// Softness: separable box-blur pada rim-mask (float buffer).
// Dijalankan 3x pass (approx. Gaussian) untuk hasil yang halus.
// radius dalam pixel (bisa fractional — di-round ke integer passes).
// ─────────────────────────────────────────────────────────────────────────────

static void BoxBlurH(double* buf, double* tmp, A_long w, A_long h, int r)
{
    if (r <= 0) return;
    const double inv = 1.0 / (2 * r + 1);
    for (A_long y = 0; y < h; ++y) {
        double* row = buf + y * w;
        double* trow = tmp + y * w;
        // build running sum for first window
        double sum = 0.0;
        for (int i = -r; i <= r; ++i) {
            int xi = i < 0 ? 0 : (i >= w ? w - 1 : i);
            sum += row[xi];
        }
        for (A_long x = 0; x < w; ++x) {
            trow[x] = sum * inv;
            int xl = x - r;     xl = xl < 0 ? 0 : xl;
            int xr = x + r + 1; xr = xr >= w ? w - 1 : xr;
            sum += row[xr] - row[xl];
        }
        for (A_long x = 0; x < w; ++x) row[x] = trow[x];
    }
}

static void BoxBlurV(double* buf, double* tmp, A_long w, A_long h, int r)
{
    if (r <= 0) return;
    const double inv = 1.0 / (2 * r + 1);
    for (A_long x = 0; x < w; ++x) {
        double sum = 0.0;
        for (int i = -r; i <= r; ++i) {
            int yi = i < 0 ? 0 : (i >= h ? h - 1 : i);
            sum += buf[yi * w + x];
        }
        for (A_long y = 0; y < h; ++y) {
            tmp[y * w + x] = sum * inv;
            int yt = y - r;     yt = yt < 0 ? 0 : yt;
            int yb = y + r + 1; yb = yb >= h ? h - 1 : yb;
            sum += buf[yb * w + x] - buf[yt * w + x];
        }
        for (A_long y = 0; y < h; ++y) buf[y * w + x] = tmp[y * w + x];
    }
}

// 3-pass box blur ≈ Gaussian. Separate X and Y radii for anisotropic blur.
static void ApplySoftness(double* mask, A_long w, A_long h, double radiusX, double radiusY)
{
    if ((radiusX <= 0.0 && radiusY <= 0.0) || w <= 0 || h <= 0) return;
    const int riX = static_cast<int>(radiusX * 0.85 + 0.5);
    const int riY = static_cast<int>(radiusY * 0.85 + 0.5);
    if (riX <= 0 && riY <= 0) return;

    double* tmp = new (std::nothrow) double[static_cast<size_t>(w) * h];
    if (!tmp) return;

    for (int pass = 0; pass < 3; ++pass) {
        if (riX > 0) BoxBlurH(mask, tmp, w, h, riX);
        if (riY > 0) BoxBlurV(mask, tmp, w, h, riY);
    }
    delete[] tmp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend composite: returns blended channel value (normalised 0–1)
//   mode 0 = Normal, 1 = Add, 2 = Screen
// src_n  = source channel normalised
// rim_n  = rim color channel normalised
// alpha  = rim alpha (already opacity-weighted)
// ─────────────────────────────────────────────────────────────────────────────

static inline double BlendChannel(double src_n, double rim_n, double alpha, A_long mode)
{
    switch (mode) {
    case RIM_BLEND_ADD:
        return src_n + rim_n * alpha;
    case RIM_BLEND_SCREEN:
        return 1.0 - (1.0 - src_n) * (1.0 - rim_n * alpha);
    case RIM_BLEND_NORMAL:
    default:
        return src_n * (1.0 - alpha) + rim_n * alpha;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Glow helpers
// ─────────────────────────────────────────────────────────────────────────────

// Smooth-step threshold: maps mask value to glow strength.
//   v < (threshold - smooth/2)          → 0
//   v > (threshold + smooth/2)          → 1
//   between                             → smooth cubic interpolation
static inline double GlowThresholdCurve(double v, double threshold, double smooth)
{
    if (smooth < 1e-6) {
        return (v >= threshold) ? ((v - threshold) / (1.0 - threshold + 1e-6)) : 0.0;
    }
    const double lo = threshold - smooth * 0.5;
    const double hi = threshold + smooth * 0.5;
    if (v <= lo) return 0.0;
    if (v >= hi) return (v - hi) / (1.0 - hi + 1e-6); // ramp above hi toward 1
    // smooth-step in [lo, hi]
    double t = (v - lo) / (hi - lo);
    t = t * t * (3.0 - 2.0 * t); // Hermite
    // above threshold portion: scale so hi maps to 0 and value ramps up
    return t * (hi - threshold) / (1.0 - threshold + 1e-6);
}

// Build glow buffer from a rim mask (float, normalised 0–1).
// Output is a float buffer of same size, NOT clamped by source alpha.
// Two-stage blur: stage1=radius, stage2=radius*GLOW_STAGE2_MULT, weighted blend.
// Separate X and Y radii for anisotropic blur.
// Returns allocated buffer (caller must delete[]), or NULL on OOM.
static double* BuildGlowBuffer(
    const double* rimMask,
    A_long w, A_long h,
    double threshold, double threshold_smooth,
    double radiusX, double radiusY)
{
    const size_t nPix = static_cast<size_t>(w) * h;

    // Extract thresholded glow source from rim mask
    double* glow = new (std::nothrow) double[nPix];
    if (!glow) return nullptr;

    for (size_t i = 0; i < nPix; ++i) {
        glow[i] = GlowThresholdCurve(rimMask[i], threshold, threshold_smooth);
    }

    // Stage-1: tight blur
    double* stage1 = new (std::nothrow) double[nPix];
    if (!stage1) { delete[] glow; return nullptr; }
    memcpy(stage1, glow, nPix * sizeof(double));
    ApplySoftness(stage1, w, h, radiusX, radiusY);

    // Stage-2: wide blur
    double* stage2 = new (std::nothrow) double[nPix];
    if (!stage2) { delete[] glow; delete[] stage1; return nullptr; }
    memcpy(stage2, glow, nPix * sizeof(double));
    ApplySoftness(stage2, w, h, radiusX * GLOW_STAGE2_MULT, radiusY * GLOW_STAGE2_MULT);

    // Merge: weighted blend — more weight on wide stage for softer glow
    const double w1 = 0.25, w2 = 0.75;
    for (size_t i = 0; i < nPix; ++i) {
        glow[i] = stage1[i] * w1 + stage2[i] * w2;
    }

    delete[] stage1;
    delete[] stage2;
    return glow;
}

// Composite one glow channel onto dst channel.
// glowCh = glow color channel (already multiplied by strength/intensity/opacity).
// type:  0=Normal(additive), 1=Add, 2=Screen
static inline double GlowBlendChannel(double dst_n, double glowCh, A_long type)
{
    switch (type) {
    case GLOW_TYPE_ADD:
    case GLOW_TYPE_NORMAL:  // glow is additive light by nature
        return dst_n + glowCh;
    case GLOW_TYPE_SCREEN:
    default:
        return 1.0 - (1.0 - dst_n) * (1.0 - glowCh);
    }
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
        PF_OutFlag_SEND_UPDATE_PARAMS_UI |
        PF_OutFlag_I_EXPAND_BUFFER;       // glow bisa melampaui bounds layer

    out_data->out_flags2 =
        PF_OutFlag2_FLOAT_COLOR_AWARE |
        PF_OutFlag2_SUPPORTS_SMART_RENDER;

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
    PF_ADD_COLOR(STR(StrID_RimColor_Param_Name), PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8, RIM_COLOR);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimSize_Param_Name),
        RIM_SIZE_MIN, RIM_SIZE_MAX, RIM_SIZE_MIN, RIM_SIZE_MAX, RIM_SIZE_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_SIZE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimOpacity_Param_Name),
        RIM_OPACITY_MIN, RIM_OPACITY_MAX, RIM_OPACITY_MIN, RIM_OPACITY_MAX, RIM_OPACITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_OPACITY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE(STR(StrID_RimAngle_Param_Name), RIM_ANGLE_DFLT, RIM_ANGLE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_RimSoftness_Param_Name),
        RIM_SOFTNESS_MIN, RIM_SOFTNESS_MAX, RIM_SOFTNESS_MIN, RIM_SOFTNESS_MAX, RIM_SOFTNESS_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, RIM_SOFTNESS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(STR(StrID_RimBlendMode_Param_Name),
        3,                       // num choices
        RIM_BLEND_DFLT + 1,      // 1-based default (AE popup is 1-based)
        "Normal|Add|Screen",     // pipe-delimited choices
        RIM_BLEND_MODE);

    // ── Glow group ────────────────────────────────────────────────────────────
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_TOPIC(STR(StrID_GlowGroup_Name), GLOW_GROUP_START);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowThreshold_Param_Name),
        GLOW_THRESHOLD_MIN, GLOW_THRESHOLD_MAX,
        GLOW_THRESHOLD_MIN, GLOW_THRESHOLD_MAX, GLOW_THRESHOLD_DFLT,
        PF_Precision_THOUSANDTHS, 0, 0, GLOW_THRESHOLD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowThresholdSmooth_Param_Name),
        GLOW_THRESHOLD_SMOOTH_MIN, GLOW_THRESHOLD_SMOOTH_MAX,
        GLOW_THRESHOLD_SMOOTH_MIN, GLOW_THRESHOLD_SMOOTH_MAX, GLOW_THRESHOLD_SMOOTH_DFLT,
        PF_Precision_THOUSANDTHS, 0, 0, GLOW_THRESHOLD_SMOOTH);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE;
    PF_ADD_CHECKBOXX(STR(StrID_GlowIndividualRadius_Param_Name),
        1, 0, GLOW_INDIVIDUAL_RADIUS);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_INVISIBLE; // hidden when individual mode is ON
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowRadius_Param_Name),
        GLOW_RADIUS_MIN, GLOW_RADIUS_MAX,
        GLOW_RADIUS_MIN, GLOW_RADIUS_MAX, GLOW_RADIUS_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_RADIUS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(STR(StrID_GlowRadiusGroup_Name),
        GLOW_RADIUS_GROUP_START);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowRadiusX_Param_Name),
        GLOW_RADIUS_X_MIN, GLOW_RADIUS_X_MAX,
        GLOW_RADIUS_X_MIN, GLOW_RADIUS_X_MAX, GLOW_RADIUS_X_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_RADIUS_X);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowRadiusY_Param_Name),
        GLOW_RADIUS_Y_MIN, GLOW_RADIUS_Y_MAX,
        GLOW_RADIUS_Y_MIN, GLOW_RADIUS_Y_MAX, GLOW_RADIUS_Y_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_RADIUS_Y);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(GLOW_RADIUS_GROUP_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowIntensity_Param_Name),
        GLOW_INTENSITY_MIN, GLOW_INTENSITY_MAX,
        GLOW_INTENSITY_MIN, GLOW_INTENSITY_MAX, GLOW_INTENSITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_INTENSITY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowSaturation_Param_Name),
        GLOW_SATURATION_MIN, GLOW_SATURATION_MAX,
        GLOW_SATURATION_MIN, GLOW_SATURATION_MAX, GLOW_SATURATION_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_SATURATION);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR(STR(StrID_GlowColor_Param_Name),
        PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8,   // default white
        GLOW_COLOR);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_GlowOpacity_Param_Name),
        GLOW_OPACITY_MIN, GLOW_OPACITY_MAX,
        GLOW_OPACITY_MIN, GLOW_OPACITY_MAX, GLOW_OPACITY_DFLT,
        PF_Precision_HUNDREDTHS, 0, 0, GLOW_OPACITY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(STR(StrID_GlowType_Param_Name),
        3,
        GLOW_TYPE_DFLT + 1,         // 1-based; Screen = index 3
        "Normal|Add|Screen",
        GLOW_TYPE);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(GLOW_GROUP_END);

    out_data->num_params = RIM_NUM_PARAMS;
    return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rim Light Render — 8-bit
//
// Konsep: fill putih digeser ke arah cahaya, lalu di-alpha matte ke layer asli.
//
//   Angle = arah datangnya cahaya (default 180 = dari kiri).
//   Fill solid digeser sejauh Size pixel ke arah BERLAWANAN angle (menjauhi cahaya).
//   Fill di-clamp oleh alpha layer asli (hanya kelihatan di dalam alpha layer).
//   Hasilnya di-composite di DEPAN layer asli.
//
//   Per pixel (x, y):
//     1. origAlpha    = alpha layer asli di (x, y)
//     2. shiftedAlpha = alpha layer asli di (x - dx, y - dy)  ← geser menjauhi cahaya
//     3. invertedFill = 1 - shiftedAlpha                      ← rim kuat di tepi menghadap cahaya
//     4. mattedFill   = min(invertedFill, origAlpha)           ← fill di-clamp alpha asli
//     5. rimAlpha     = mattedFill * opacity
//     6. Composite: rimColor over layer asli, dengan rimAlpha
//     7. outAlpha     = origAlpha  (alpha tidak berubah, rim hanya di dalam layer)
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
    const double dist = info.rim_size;
    const double opacity = info.rim_opacity / 100.0;

    // Arah geser fill: KE ARAH BERLAWANAN cahaya
    const double dx = cos(angle_rad) * dist;
    const double dy = -sin(angle_rad) * dist;

    const double rimR_n = info.rim_color.red / 255.0;
    const double rimG_n = info.rim_color.green / 255.0;
    const double rimB_n = info.rim_color.blue / 255.0;

    // ── Pass 1: build rim mask (mattedFill) and glow mask (invertedFill, un-matted) ──
    const size_t nPix = static_cast<size_t>(width) * height;
    double* mask = new (std::nothrow) double[nPix];
    double* glowMask = new (std::nothrow) double[nPix];
    if (!mask || !glowMask) { delete[] mask; delete[] glowMask; return PF_Err_OUT_OF_MEMORY; }

    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel8& sp = src_base[y * src_stride + x];
            const double origAlpha = sp.alpha / 255.0;

            const double sx_f = (double)x - dx;
            const double sy_f = (double)y - dy;
            const int sx0 = (int)floor(sx_f);
            const int sy0 = (int)floor(sy_f);
            const double fx = sx_f - (double)sx0;
            const double fy = sy_f - (double)sy0;

            auto safeA = [&](int px, int py) -> double {
                if (px < 0 || px >= width || py < 0 || py >= height) return 0.0;
                return src_base[py * src_stride + px].alpha / 255.0;
                };

            const double shiftedAlpha =
                safeA(sx0, sy0) * (1.0 - fx) * (1.0 - fy) +
                safeA(sx0 + 1, sy0) * fx * (1.0 - fy) +
                safeA(sx0, sy0 + 1) * (1.0 - fx) * fy +
                safeA(sx0 + 1, sy0 + 1) * fx * fy;

            const double invertedFill = 1.0 - shiftedAlpha;
            const double mattedFill =
                (origAlpha > 0.001)
                ? invertedFill
                : 0.0;
            // glowMask pakai invertedFill (un-matted) agar blur glow menyebar halus dari
            // tepi alpha layer tanpa hard-edge. Compositing-nya nanti di-weight oleh
            // origAlpha saat ApplyGlow, sehingga glow tidak bocor ke area fully transparan.
            glowMask[y * width + x] = invertedFill * origAlpha; // pre-multiply: lembut di tepi semi-transparan
            mask[y * width + x] = mattedFill;
        }
    }

    // ── Pass 2: softness hanya pada rim mask; glow mask sengaja TIDAK di-blur softness ──
    // Glow punya blur sendiri (BuildGlowBuffer), dan harus mulai dari tepi yang bersih.
    ApplySoftness(mask, width, height, info.rim_softness, info.rim_softness);

    // ── Pass 3: composite rim onto src → write to dst ────────────────────────
    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel8& sp = src_base[y * src_stride + x];
            PF_Pixel8& dp = dst_base[y * dst_stride + x];

            // FIX: always copy src first so dst RGB is never garbage.
            // Skip blend entirely for fully-transparent pixels so we don't
            // corrupt dst RGB — Pass 4 (glow) uses sp.alpha to distinguish
            // transparent vs opaque regions.
            dp = sp;
            if (sp.alpha == 0) continue;

            const double rimAlpha = mask[y * width + x] * opacity;

            if (rimAlpha > 0) {
                dp.red = clamp255((int)(BlendChannel(sp.red / 255.0, rimR_n, rimAlpha, info.rim_blend_mode) * 255.0));
                dp.green = clamp255((int)(BlendChannel(sp.green / 255.0, rimG_n, rimAlpha, info.rim_blend_mode) * 255.0));
                dp.blue = clamp255((int)(BlendChannel(sp.blue / 255.0, rimB_n, rimAlpha, info.rim_blend_mode) * 255.0));
                dp.alpha = sp.alpha;
            }
        }
    }

    // ── Pass 4: glow (uses un-matted glowMask, not clamped by source alpha) ──
    const double glowRX_8 = info.glow_individual ? info.glow_radius_x : info.glow_radius;
    const double glowRY_8 = info.glow_individual ? info.glow_radius_y : info.glow_radius;
    if (info.glow_intensity > 0.0 && info.glow_opacity > 0.0 && (glowRX_8 > 0.0 || glowRY_8 > 0.0)) {
        double* glowBuf = BuildGlowBuffer(glowMask, width, height,
            info.glow_threshold, info.glow_threshold_smooth, glowRX_8, glowRY_8);
        if (glowBuf) {
            const double glowR_n = info.glow_color.red / 255.0;
            const double glowG_n = info.glow_color.green / 255.0;
            const double glowB_n = info.glow_color.blue / 255.0;

            for (A_long y = 0; y < height; ++y) {
                for (A_long x = 0; x < width; ++x) {
                    PF_Pixel8& dp = dst_base[y * dst_stride + x];
                    // FIX: use original src alpha (not dp.alpha which may have been
                    // modified by Pass 3) to correctly detect transparent regions.
                    const PF_Pixel8& sp = src_base[y * src_stride + x];

                    double g = glowBuf[y * width + x] * info.glow_intensity * info.glow_opacity;
                    if (g <= 0.0) continue;
                    if (g > 1.0) g = 1.0;

                    const A_u_char glowAlpha8 = clamp255((int)(g * 255.0));
                    if (glowAlpha8 == 0) continue;

                    if (sp.alpha == 0) {
                        // Unpremult: tulis glow color FULL (tanpa dikali g) dengan alpha
                        // parsial. AE composite: result = glow * g + bg * (1-g).
                        // White glow on white bg → white (tidak meredup).
                        // Colored glow on white bg → soft tint (blend natural).
                        dp.red = clamp255((int)(glowR_n * 255.0));
                        dp.green = clamp255((int)(glowG_n * 255.0));
                        dp.blue = clamp255((int)(glowB_n * 255.0));
                        dp.alpha = glowAlpha8;
                    }
                    else {
                        // Inside source: attenuate glow agar outside bias lebih dominan
                        const double gIn = g * GLOW_INSIDE_ATTEN;
                        if (gIn <= 0.0) continue;
                        const double dR = dp.red / 255.0;
                        const double dG = dp.green / 255.0;
                        const double dB = dp.blue / 255.0;
                        const double lum = dR * 0.2126 + dG * 0.7152 + dB * 0.0722;
                        const double cR = glowR_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                        const double cG = glowG_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                        const double cB = glowB_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;

                        dp.red = clamp255((int)(GlowBlendChannel(dR, cR * gIn, info.glow_type) * 255.0));
                        dp.green = clamp255((int)(GlowBlendChannel(dG, cG * gIn, info.glow_type) * 255.0));
                        dp.blue = clamp255((int)(GlowBlendChannel(dB, cB * gIn, info.glow_type) * 255.0));
                        if (glowAlpha8 > dp.alpha) dp.alpha = glowAlpha8;
                    }
                }
            }
            delete[] glowBuf;
        }
    }

    delete[] glowMask;
    delete[] mask;
    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rim Light Render — 16-bit
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
    const double dist = info.rim_size;
    const double opacity = info.rim_opacity / 100.0;

    const double dx = cos(angle_rad) * dist;
    const double dy = -sin(angle_rad) * dist;

    const double rimR_n = info.rim_color.red / 255.0;
    const double rimG_n = info.rim_color.green / 255.0;
    const double rimB_n = info.rim_color.blue / 255.0;

    // ── Pass 1: build rim mask and glow mask (un-matted) ───────────────────────
    const size_t nPix = static_cast<size_t>(width) * height;
    double* mask = new (std::nothrow) double[nPix];
    double* glowMask = new (std::nothrow) double[nPix];
    if (!mask || !glowMask) { delete[] mask; delete[] glowMask; return PF_Err_OUT_OF_MEMORY; }

    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel16& sp = src_base[y * src_stride + x];
            const double origAlpha = sp.alpha / 32768.0;

            const double sx_f = (double)x - dx;
            const double sy_f = (double)y - dy;
            const int sx0 = (int)floor(sx_f);
            const int sy0 = (int)floor(sy_f);
            const double fx = sx_f - (double)sx0;
            const double fy = sy_f - (double)sy0;

            auto safeA = [&](int px, int py) -> double {
                if (px < 0 || px >= width || py < 0 || py >= height) return 0.0;
                return src_base[py * src_stride + px].alpha / 32768.0;
                };

            const double shiftedAlpha =
                safeA(sx0, sy0) * (1.0 - fx) * (1.0 - fy) +
                safeA(sx0 + 1, sy0) * fx * (1.0 - fy) +
                safeA(sx0, sy0 + 1) * (1.0 - fx) * fy +
                safeA(sx0 + 1, sy0 + 1) * fx * fy;

            const double invertedFill = 1.0 - shiftedAlpha;
            const double mattedFill =
                (origAlpha > 0.001)
                ? invertedFill
                : 0.0;
            // glowMask pakai invertedFill pre-multiplied dengan origAlpha: halus di tepi semi-transparan.
            glowMask[y * width + x] = invertedFill * origAlpha;
            mask[y * width + x] = mattedFill;
        }
    }

    // ── Pass 2: softness hanya pada rim mask ──────────────────────────────────
    ApplySoftness(mask, width, height, info.rim_softness, info.rim_softness);

    // ── Pass 3: composite rim → dst ───────────────────────────────────────────
    for (A_long y = 0; y < height; ++y) {
        for (A_long x = 0; x < width; ++x) {
            const PF_Pixel16& sp = src_base[y * src_stride + x];
            PF_Pixel16& dp = dst_base[y * dst_stride + x];

            // FIX: always copy src first so dst RGB is never garbage.
            // Skip blend for fully-transparent pixels to avoid corrupting dst RGB.
            dp = sp;
            if (sp.alpha == 0) continue;

            const double rimAlpha = mask[y * width + x] * opacity;

            if (rimAlpha > 0) {
                dp.red = clamp32768((int)(BlendChannel(sp.red / 32768.0, rimR_n, rimAlpha, info.rim_blend_mode) * 32768.0));
                dp.green = clamp32768((int)(BlendChannel(sp.green / 32768.0, rimG_n, rimAlpha, info.rim_blend_mode) * 32768.0));
                dp.blue = clamp32768((int)(BlendChannel(sp.blue / 32768.0, rimB_n, rimAlpha, info.rim_blend_mode) * 32768.0));
                dp.alpha = 32768;
            }
        }
    }

    // ── Pass 4: glow (uses un-matted glowMask) ────────────────────────────────
    const double glowRX_16 = info.glow_individual ? info.glow_radius_x : info.glow_radius;
    const double glowRY_16 = info.glow_individual ? info.glow_radius_y : info.glow_radius;
    if (info.glow_intensity > 0.0 && info.glow_opacity > 0.0 && (glowRX_16 > 0.0 || glowRY_16 > 0.0)) {
        double* glowBuf = BuildGlowBuffer(glowMask, width, height,
            info.glow_threshold, info.glow_threshold_smooth, glowRX_16, glowRY_16);
        if (glowBuf) {
            const double glowR_n = info.glow_color.red / 255.0;
            const double glowG_n = info.glow_color.green / 255.0;
            const double glowB_n = info.glow_color.blue / 255.0;

            for (A_long y = 0; y < height; ++y) {
                for (A_long x = 0; x < width; ++x) {
                    PF_Pixel16& dp = dst_base[y * dst_stride + x];
                    // FIX: use original src alpha (not dp.alpha which was modified
                    // by Pass 3) to correctly detect transparent regions.
                    const PF_Pixel16& sp = src_base[y * src_stride + x];

                    double g = glowBuf[y * width + x] * info.glow_intensity * info.glow_opacity;
                    if (g <= 0.0) continue;
                    if (g > 1.0) g = 1.0;

                    const A_u_short glowAlpha16 = clamp32768((int)(g * 32768.0));
                    if (glowAlpha16 == 0) continue;

                    if (sp.alpha == 0) {
                        // Unpremult: full glow color, partial alpha → soft blend over bg
                        dp.red = clamp32768((int)(glowR_n * 32768.0));
                        dp.green = clamp32768((int)(glowG_n * 32768.0));
                        dp.blue = clamp32768((int)(glowB_n * 32768.0));
                        dp.alpha = glowAlpha16;
                    }
                    else {
                        // Inside source: attenuate glow agar outside bias lebih dominan
                        const double gIn = g * GLOW_INSIDE_ATTEN;
                        if (gIn <= 0.0) continue;
                        const double dR = dp.red / 32768.0;
                        const double dG = dp.green / 32768.0;
                        const double dB = dp.blue / 32768.0;
                        const double lum = dR * 0.2126 + dG * 0.7152 + dB * 0.0722;
                        const double cR = glowR_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                        const double cG = glowG_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                        const double cB = glowB_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;

                        dp.red = clamp32768((int)(GlowBlendChannel(dR, cR * gIn, info.glow_type) * 32768.0));
                        dp.green = clamp32768((int)(GlowBlendChannel(dG, cG * gIn, info.glow_type) * 32768.0));
                        dp.blue = clamp32768((int)(GlowBlendChannel(dB, cB * gIn, info.glow_type) * 32768.0));
                        if (glowAlpha16 > dp.alpha) dp.alpha = glowAlpha16;
                    }
                }
            }
            delete[] glowBuf;
        }
    }

    delete[] glowMask;
    delete[] mask;
    return PF_Err_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateParamUI — toggle visibility of Radius / Separate-Radius group + X/Y
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err UpdateParamUI(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    const A_long individual = params[GLOW_INDIVIDUAL_RADIUS]->u.bd.value;

    if (individual) {
        params[GLOW_RADIUS]->flags |= PF_ParamFlag_INVISIBLE;
        params[GLOW_RADIUS_GROUP_START]->flags &= ~PF_ParamFlag_INVISIBLE;
        params[GLOW_RADIUS_X]->flags &= ~PF_ParamFlag_INVISIBLE;
        params[GLOW_RADIUS_Y]->flags &= ~PF_ParamFlag_INVISIBLE;
    }
    else {
        params[GLOW_RADIUS]->flags &= ~PF_ParamFlag_INVISIBLE;
        params[GLOW_RADIUS_GROUP_START]->flags |= PF_ParamFlag_INVISIBLE;
        params[GLOW_RADIUS_X]->flags |= PF_ParamFlag_INVISIBLE;
        params[GLOW_RADIUS_Y]->flags |= PF_ParamFlag_INVISIBLE;
    }
    return err;
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

    // Scale pixel-based parameters to current preview resolution
    const double ds_x = (double)in_data->downsample_x.num / (double)in_data->downsample_x.den;
    const double ds_y = (double)in_data->downsample_y.num / (double)in_data->downsample_y.den;
    const double ds = (ds_x < ds_y) ? ds_x : ds_y; // use smaller axis for isotropic params

    info.rim_color = params[RIM_COLOR]->u.cd.value;
    info.rim_size = params[RIM_SIZE]->u.fs_d.value * ds_x;
    info.rim_opacity = params[RIM_OPACITY]->u.fs_d.value;
    info.rim_angle = params[RIM_ANGLE]->u.ad.value / 65536.0;
    info.rim_softness = params[RIM_SOFTNESS]->u.fs_d.value * ds;
    info.rim_blend_mode = params[RIM_BLEND_MODE]->u.pd.value - 1; // AE popup is 1-based

    info.glow_threshold = params[GLOW_THRESHOLD]->u.fs_d.value;
    info.glow_threshold_smooth = params[GLOW_THRESHOLD_SMOOTH]->u.fs_d.value;
    info.glow_individual = params[GLOW_INDIVIDUAL_RADIUS]->u.bd.value;
    info.glow_radius = params[GLOW_RADIUS]->u.fs_d.value * ds;
    info.glow_radius_x = params[GLOW_RADIUS_X]->u.fs_d.value * ds_x;
    info.glow_radius_y = params[GLOW_RADIUS_Y]->u.fs_d.value * ds_y;
    info.glow_intensity = params[GLOW_INTENSITY]->u.fs_d.value;
    info.glow_saturation = params[GLOW_SATURATION]->u.fs_d.value;
    info.glow_color = params[GLOW_COLOR]->u.cd.value;
    info.glow_opacity = params[GLOW_OPACITY]->u.fs_d.value;
    info.glow_type = params[GLOW_TYPE]->u.pd.value - 1; // 1-based

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
//
// Kita perlu sample alpha di (x - dx, y - dy) — bisa jatuh di luar tile.
// Solusi: minta AE suplai input yang lebih besar dari output tile sebesar
// ceil(rim_size) ke semua sisi (checkout_rect di-expand), sehingga
// safeA tidak pernah keluar dari data yang disediakan AE.
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err PreRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    // Baca rim_size, rim_softness, dan glow_radius dari parameter
    PF_ParamDef rimSizeParam, rimSoftnessParam;
    PF_ParamDef glowIndividualParam, glowRadiusParam, glowRadiusXParam, glowRadiusYParam;
    AEFX_CLR_STRUCT(rimSizeParam);
    AEFX_CLR_STRUCT(rimSoftnessParam);
    AEFX_CLR_STRUCT(glowIndividualParam);
    AEFX_CLR_STRUCT(glowRadiusParam);
    AEFX_CLR_STRUCT(glowRadiusXParam);
    AEFX_CLR_STRUCT(glowRadiusYParam);
    ERR(PF_CHECKOUT_PARAM(in_data, RIM_SIZE, in_data->current_time,
        in_data->time_step, in_data->time_scale, &rimSizeParam));
    ERR(PF_CHECKOUT_PARAM(in_data, RIM_SOFTNESS, in_data->current_time,
        in_data->time_step, in_data->time_scale, &rimSoftnessParam));
    ERR(PF_CHECKOUT_PARAM(in_data, GLOW_INDIVIDUAL_RADIUS, in_data->current_time,
        in_data->time_step, in_data->time_scale, &glowIndividualParam));
    ERR(PF_CHECKOUT_PARAM(in_data, GLOW_RADIUS, in_data->current_time,
        in_data->time_step, in_data->time_scale, &glowRadiusParam));
    ERR(PF_CHECKOUT_PARAM(in_data, GLOW_RADIUS_X, in_data->current_time,
        in_data->time_step, in_data->time_scale, &glowRadiusXParam));
    ERR(PF_CHECKOUT_PARAM(in_data, GLOW_RADIUS_Y, in_data->current_time,
        in_data->time_step, in_data->time_scale, &glowRadiusYParam));

    const double rimSizeVal = rimSizeParam.u.fs_d.value;
    const double softRadius = err ? 0.0 : rimSoftnessParam.u.fs_d.value * 0.85 * 3.0;
    // Compute effective glow radius for padding (use max of X/Y)
    const A_long individual = err ? 0 : glowIndividualParam.u.bd.value;
    const double glowR = err ? 0.0 :
        (individual ? fmax(glowRadiusXParam.u.fs_d.value, glowRadiusYParam.u.fs_d.value) :
            glowRadiusParam.u.fs_d.value);
    // Stage-2 glow = radius * GLOW_STAGE2_MULT, 3 box-blur passes each side
    const double glowRadius = glowR * GLOW_STAGE2_MULT * 0.85 * 3.0;
    const A_long pad = err ? 64 :
        static_cast<A_long>(ceil(rimSizeVal + softRadius + glowRadius)) + 4;
    const A_long rimSizePx = err ? 0 : static_cast<A_long>(ceil(rimSizeVal));

    PF_CHECKIN_PARAM(in_data, &rimSizeParam);
    PF_CHECKIN_PARAM(in_data, &rimSoftnessParam);
    PF_CHECKIN_PARAM(in_data, &glowIndividualParam);
    PF_CHECKIN_PARAM(in_data, &glowRadiusParam);
    PF_CHECKIN_PARAM(in_data, &glowRadiusXParam);
    PF_CHECKIN_PARAM(in_data, &glowRadiusYParam);

    // Expand output rect agar glow bisa melampaui bounds layer
    // result_rect harus tetap dalam output_request.rect (AE SDK rule).
    // max_result_rect boleh lebih besar — memberitahu AE bahwa effect
    // bisa menulis pixel di luar area request (dengan flag I_EXPAND_BUFFER).
    const A_long outPad = pad;
    PF_LRect max_rect = extra->input->output_request.rect;
    max_rect.left   -= outPad;
    max_rect.top    -= outPad;
    max_rect.right  += outPad;
    max_rect.bottom += outPad;
    extra->output->result_rect = extra->input->output_request.rect;
    extra->output->max_result_rect = max_rect;
    extra->output->solid = FALSE;
    extra->output->pre_render_data = NULL;

    // Checkout input lebih besar lagi: butuh extra rimSize untuk sampling rim
    // dari pixel di tepi expanded output.
    const A_long inPad = pad + rimSizePx;
    PF_RenderRequest req = extra->input->output_request;
    req.rect.left   -= inPad;
    req.rect.top    -= inPad;
    req.rect.right  += inPad;
    req.rect.bottom += inPad;
    req.preserve_rgb_of_zero_alpha = FALSE;

    // checkout_layer argumen ke-8 adalah PF_CheckoutResult*, bukan PF_LRect*
    PF_CheckoutResult checkout_result;
    AEFX_CLR_STRUCT(checkout_result);

    ERR(extra->cb->checkout_layer(in_data->effect_ref,
        RIM_INPUT,
        RIM_INPUT,
        &req,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &checkout_result));
    // result_rect dan max_result_rect sudah di-set ke tile output di atas.

    return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// SmartRender — dipanggil saat full render (SUPPORTS_SMART_RENDER aktif)
// Input layer sudah di-checkout oleh PreRender dengan rect yang di-expand.
// ─────────────────────────────────────────────────────────────────────────────

static PF_Err SmartRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    PF_EffectWorld* input_world = NULL;
    PF_EffectWorld* output_world = NULL;

    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, RIM_INPUT, &input_world));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &output_world));

    if (!err && input_world && output_world) {
        RimLightInfo info;
        AEFX_CLR_STRUCT(info);

        PF_ParamDef colorParam, sizeParam, opacityParam, angleParam, softnessParam, blendParam;
        PF_ParamDef glowThreshParam, glowThreshSmoothParam, glowIndividualParam;
        PF_ParamDef glowRadiusParam, glowRadiusXParam, glowRadiusYParam;
        PF_ParamDef glowIntensityParam, glowSaturationParam, glowColorParam;
        PF_ParamDef glowOpacityParam, glowTypeParam;
        AEFX_CLR_STRUCT(colorParam);
        AEFX_CLR_STRUCT(sizeParam);
        AEFX_CLR_STRUCT(opacityParam);
        AEFX_CLR_STRUCT(angleParam);
        AEFX_CLR_STRUCT(softnessParam);
        AEFX_CLR_STRUCT(blendParam);
        AEFX_CLR_STRUCT(glowThreshParam);
        AEFX_CLR_STRUCT(glowThreshSmoothParam);
        AEFX_CLR_STRUCT(glowIndividualParam);
        AEFX_CLR_STRUCT(glowRadiusParam);
        AEFX_CLR_STRUCT(glowRadiusXParam);
        AEFX_CLR_STRUCT(glowRadiusYParam);
        AEFX_CLR_STRUCT(glowIntensityParam);
        AEFX_CLR_STRUCT(glowSaturationParam);
        AEFX_CLR_STRUCT(glowColorParam);
        AEFX_CLR_STRUCT(glowOpacityParam);
        AEFX_CLR_STRUCT(glowTypeParam);

        ERR(PF_CHECKOUT_PARAM(in_data, RIM_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &colorParam));
        ERR(PF_CHECKOUT_PARAM(in_data, RIM_SIZE, in_data->current_time, in_data->time_step, in_data->time_scale, &sizeParam));
        ERR(PF_CHECKOUT_PARAM(in_data, RIM_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &opacityParam));
        ERR(PF_CHECKOUT_PARAM(in_data, RIM_ANGLE, in_data->current_time, in_data->time_step, in_data->time_scale, &angleParam));
        ERR(PF_CHECKOUT_PARAM(in_data, RIM_SOFTNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &softnessParam));
        ERR(PF_CHECKOUT_PARAM(in_data, RIM_BLEND_MODE, in_data->current_time, in_data->time_step, in_data->time_scale, &blendParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_THRESHOLD, in_data->current_time, in_data->time_step, in_data->time_scale, &glowThreshParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_THRESHOLD_SMOOTH, in_data->current_time, in_data->time_step, in_data->time_scale, &glowThreshSmoothParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_INDIVIDUAL_RADIUS, in_data->current_time, in_data->time_step, in_data->time_scale, &glowIndividualParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_RADIUS, in_data->current_time, in_data->time_step, in_data->time_scale, &glowRadiusParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_RADIUS_X, in_data->current_time, in_data->time_step, in_data->time_scale, &glowRadiusXParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_RADIUS_Y, in_data->current_time, in_data->time_step, in_data->time_scale, &glowRadiusYParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_INTENSITY, in_data->current_time, in_data->time_step, in_data->time_scale, &glowIntensityParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_SATURATION, in_data->current_time, in_data->time_step, in_data->time_scale, &glowSaturationParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &glowColorParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &glowOpacityParam));
        ERR(PF_CHECKOUT_PARAM(in_data, GLOW_TYPE, in_data->current_time, in_data->time_step, in_data->time_scale, &glowTypeParam));

        if (!err) {
            info.rim_color = colorParam.u.cd.value;
            info.rim_size = sizeParam.u.fs_d.value;
            info.rim_opacity = opacityParam.u.fs_d.value;
            info.rim_angle = angleParam.u.ad.value / 65536.0;
            info.rim_softness = softnessParam.u.fs_d.value;
            info.rim_blend_mode = blendParam.u.pd.value - 1; // AE popup is 1-based

            info.glow_threshold = glowThreshParam.u.fs_d.value;
            info.glow_threshold_smooth = glowThreshSmoothParam.u.fs_d.value;
            info.glow_individual = glowIndividualParam.u.bd.value;
            info.glow_radius = glowRadiusParam.u.fs_d.value;
            info.glow_radius_x = glowRadiusXParam.u.fs_d.value;
            info.glow_radius_y = glowRadiusYParam.u.fs_d.value;
            info.glow_intensity = glowIntensityParam.u.fs_d.value;
            info.glow_saturation = glowSaturationParam.u.fs_d.value;
            info.glow_color = glowColorParam.u.cd.value;
            info.glow_opacity = glowOpacityParam.u.fs_d.value;
            info.glow_type = glowTypeParam.u.pd.value - 1; // 1-based

            // ----------------------------------------------------------------
            // Coordinate mapping: output pixel (x,y) -> input buffer (ix,iy)
            //
            // origin_x/y in AE SmartRender = comp-space coordinates (top-left
            // of each buffer in composition space). The difference gives how
            // many pixels output (0,0) is offset inside the input buffer.
            //
            //   ix = x + (output->origin_x - input->origin_x)
            //   iy = y + (output->origin_y - input->origin_y)
            //
            // This is stable even when layer size != comp size because both
            // origins are in the same comp-space coordinate system.
            //
            // Downsample fix: rim_size is in full-resolution pixels.
            // At lower quality, AE scales all world coordinates, so dx/dy
            // must also be scaled to match the buffer resolution.
            // ----------------------------------------------------------------
            const double ds_x = (double)in_data->downsample_x.num / (double)in_data->downsample_x.den;
            const double ds_y = (double)in_data->downsample_y.num / (double)in_data->downsample_y.den;

            const A_long off_x = output_world->origin_x - input_world->origin_x;
            const A_long off_y = output_world->origin_y - input_world->origin_y;

            const double angle_rad = info.rim_angle * 3.14159265358979323846 / 180.0;
            const double dx = cos(angle_rad) * info.rim_size * ds_x;
            const double dy = -sin(angle_rad) * info.rim_size * ds_y;
            const double opacity = info.rim_opacity / 100.0;

            if (PF_WORLD_IS_DEEP(output_world)) {
                const A_long out_w = output_world->width;
                const A_long out_h = output_world->height;
                const A_long in_w = input_world->width;
                const A_long in_h = input_world->height;
                const A_long src_stride = input_world->rowbytes / sizeof(PF_Pixel16);
                const A_long dst_stride = output_world->rowbytes / sizeof(PF_Pixel16);
                const PF_Pixel16* src_base = reinterpret_cast<const PF_Pixel16*>(input_world->data);
                PF_Pixel16* dst_base = reinterpret_cast<PF_Pixel16*>(output_world->data);

                const double rimR_n = info.rim_color.red / 255.0;
                const double rimG_n = info.rim_color.green / 255.0;
                const double rimB_n = info.rim_color.blue / 255.0;

                // ── Pass 1: rim mask + glow mask (un-matted) ──────────────────────
                const size_t nPix = static_cast<size_t>(out_w) * out_h;
                double* mask = new (std::nothrow) double[nPix];
                double* glowMask = new (std::nothrow) double[nPix];
                if (!mask || !glowMask) { delete[] mask; delete[] glowMask; err = PF_Err_OUT_OF_MEMORY; goto checkin_params; }

                for (A_long y = 0; y < out_h; ++y) {
                    for (A_long x = 0; x < out_w; ++x) {
                        const A_long ix = x + off_x;
                        const A_long iy = y + off_y;

                        if (ix < 0 || ix >= in_w || iy < 0 || iy >= in_h) {
                            mask[y * out_w + x] = 0.0;
                            glowMask[y * out_w + x] = 0.0;
                            dst_base[y * dst_stride + x] = {};
                            continue;
                        }

                        const PF_Pixel16& sp = src_base[iy * src_stride + ix];
                        const double origAlpha = sp.alpha / 32768.0;

                        const double sx_f = (double)ix - dx;
                        const double sy_f = (double)iy - dy;
                        const int sx0 = (int)floor(sx_f);
                        const int sy0 = (int)floor(sy_f);
                        const double fx = sx_f - sx0;
                        const double fy = sy_f - sy0;

                        auto safeA = [&](int px, int py) -> double {
                            if (px < 0 || px >= in_w || py < 0 || py >= in_h) return 0.0;
                            return src_base[py * src_stride + px].alpha / 32768.0;
                            };

                        const double shiftedAlpha =
                            safeA(sx0, sy0) * (1.0 - fx) * (1.0 - fy) +
                            safeA(sx0 + 1, sy0) * fx * (1.0 - fy) +
                            safeA(sx0, sy0 + 1) * (1.0 - fx) * fy +
                            safeA(sx0 + 1, sy0 + 1) * fx * fy;

                        const double invertedFill = 1.0 - shiftedAlpha;
                        const double mattedFill =
                            (origAlpha > 0.001)
                            ? invertedFill
                            : 0.0;
                        glowMask[y * out_w + x] = invertedFill * origAlpha; // pre-multiply: smooth glow di tepi semi-transparan
                        mask[y * out_w + x] = mattedFill;
                    }
                }

                // ── Pass 2: softness hanya pada rim mask ─────────────────────────
                ApplySoftness(mask, out_w, out_h, info.rim_softness, info.rim_softness);

                // ── Pass 3: composite ─────────────────────────────────────────
                for (A_long y = 0; y < out_h; ++y) {
                    for (A_long x = 0; x < out_w; ++x) {
                        const A_long ix = x + off_x;
                        const A_long iy = y + off_y;
                        if (ix < 0 || ix >= in_w || iy < 0 || iy >= in_h) continue;

                        const PF_Pixel16& sp = src_base[iy * src_stride + ix];
                        PF_Pixel16& dp = dst_base[y * dst_stride + x];

                        // FIX: copy src first; skip blend for fully-transparent pixels.
                        dp = sp;
                        if (sp.alpha == 0) continue;

                        const double rimAlpha = mask[y * out_w + x] * opacity;

                        if (rimAlpha > 0) {
                            dp.red = clamp32768((int)(BlendChannel(sp.red / 32768.0, rimR_n, rimAlpha, info.rim_blend_mode) * 32768.0));
                            dp.green = clamp32768((int)(BlendChannel(sp.green / 32768.0, rimG_n, rimAlpha, info.rim_blend_mode) * 32768.0));
                            dp.blue = clamp32768((int)(BlendChannel(sp.blue / 32768.0, rimB_n, rimAlpha, info.rim_blend_mode) * 32768.0));
                            dp.alpha = 32768;
                        }
                    }
                }

                // ── Pass 4: glow (uses un-matted glowMask) ──────────────────────
                const double ds = fmin(ds_x, ds_y);
                const double glowRX_16s = info.glow_individual ? info.glow_radius_x : info.glow_radius;
                const double glowRY_16s = info.glow_individual ? info.glow_radius_y : info.glow_radius;
                if (info.glow_intensity > 0.0 && info.glow_opacity > 0.0 && (glowRX_16s > 0.0 || glowRY_16s > 0.0)) {
                    // Scale radius to current downsample resolution
                    const double glowR_dsX = glowRX_16s * (info.glow_individual ? ds_x : ds);
                    const double glowR_dsY = glowRY_16s * (info.glow_individual ? ds_y : ds);
                    double* glowBuf = BuildGlowBuffer(glowMask, out_w, out_h,
                        info.glow_threshold, info.glow_threshold_smooth, glowR_dsX, glowR_dsY);
                    if (glowBuf) {
                        const double glowR_n = info.glow_color.red / 255.0;
                        const double glowG_n = info.glow_color.green / 255.0;
                        const double glowB_n = info.glow_color.blue / 255.0;

                        for (A_long y = 0; y < out_h; ++y) {
                            for (A_long x = 0; x < out_w; ++x) {
                                PF_Pixel16& dp = dst_base[y * dst_stride + x];
                                // FIX: look up src alpha via origin offset — dp.alpha may
                                // have been modified by Pass 3, so we read the original.
                                const A_long ix = x + off_x;
                                const A_long iy = y + off_y;
                                const A_u_short srcAlpha16 = (ix >= 0 && ix < in_w && iy >= 0 && iy < in_h)
                                    ? src_base[iy * src_stride + ix].alpha : 0;

                                double g = glowBuf[y * out_w + x] * info.glow_intensity * info.glow_opacity;
                                if (g <= 0.0) continue;
                                if (g > 1.0) g = 1.0;

                                const A_u_short glowAlpha16 = clamp32768((int)(g * 32768.0));
                                if (glowAlpha16 == 0) continue;

                                if (srcAlpha16 == 0) {
                                    // Unpremult: full glow color, partial alpha → soft blend
                                    dp.red = clamp32768((int)(glowR_n * 32768.0));
                                    dp.green = clamp32768((int)(glowG_n * 32768.0));
                                    dp.blue = clamp32768((int)(glowB_n * 32768.0));
                                    dp.alpha = glowAlpha16;
                                }
                                else {
                                    // Inside source: attenuate agar outside bias dominan
                                    const double gIn = g * GLOW_INSIDE_ATTEN;
                                    if (gIn <= 0.0) continue;
                                    const double dR = dp.red / 32768.0;
                                    const double dG = dp.green / 32768.0;
                                    const double dB = dp.blue / 32768.0;
                                    const double lum = dR * 0.2126 + dG * 0.7152 + dB * 0.0722;
                                    const double cR = glowR_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                                    const double cG = glowG_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                                    const double cB = glowB_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;

                                    dp.red = clamp32768((int)(GlowBlendChannel(dR, cR * gIn, info.glow_type) * 32768.0));
                                    dp.green = clamp32768((int)(GlowBlendChannel(dG, cG * gIn, info.glow_type) * 32768.0));
                                    dp.blue = clamp32768((int)(GlowBlendChannel(dB, cB * gIn, info.glow_type) * 32768.0));
                                    if (glowAlpha16 > dp.alpha) dp.alpha = glowAlpha16;
                                }
                            }
                        }
                        delete[] glowBuf;
                    }
                }

                delete[] glowMask;
                delete[] mask;
            }
            else {
                const A_long out_w = output_world->width;
                const A_long out_h = output_world->height;
                const A_long in_w = input_world->width;
                const A_long in_h = input_world->height;
                const A_long src_stride = input_world->rowbytes / sizeof(PF_Pixel8);
                const A_long dst_stride = output_world->rowbytes / sizeof(PF_Pixel8);
                const PF_Pixel8* src_base = reinterpret_cast<const PF_Pixel8*>(input_world->data);
                PF_Pixel8* dst_base = reinterpret_cast<PF_Pixel8*>(output_world->data);

                const double rimR_n = info.rim_color.red / 255.0;
                const double rimG_n = info.rim_color.green / 255.0;
                const double rimB_n = info.rim_color.blue / 255.0;

                // ── Pass 1: rim mask + glow mask (un-matted) ──────────────────────
                const size_t nPix = static_cast<size_t>(out_w) * out_h;
                double* mask = new (std::nothrow) double[nPix];
                double* glowMask = new (std::nothrow) double[nPix];
                if (!mask || !glowMask) { delete[] mask; delete[] glowMask; err = PF_Err_OUT_OF_MEMORY; goto checkin_params; }

                for (A_long y = 0; y < out_h; ++y) {
                    for (A_long x = 0; x < out_w; ++x) {
                        const A_long ix = x + off_x;
                        const A_long iy = y + off_y;

                        if (ix < 0 || ix >= in_w || iy < 0 || iy >= in_h) {
                            mask[y * out_w + x] = 0.0;
                            glowMask[y * out_w + x] = 0.0;
                            dst_base[y * dst_stride + x] = {};
                            continue;
                        }

                        const PF_Pixel8& sp = src_base[iy * src_stride + ix];
                        const double origAlpha = sp.alpha / 255.0;

                        const double sx_f = (double)ix - dx;
                        const double sy_f = (double)iy - dy;
                        const int sx0 = (int)floor(sx_f);
                        const int sy0 = (int)floor(sy_f);
                        const double fx = sx_f - sx0;
                        const double fy = sy_f - sy0;

                        auto safeA = [&](int px, int py) -> double {
                            if (px < 0 || px >= in_w || py < 0 || py >= in_h) return 0.0;
                            return src_base[py * src_stride + px].alpha / 255.0;
                            };

                        const double shiftedAlpha =
                            safeA(sx0, sy0) * (1.0 - fx) * (1.0 - fy) +
                            safeA(sx0 + 1, sy0) * fx * (1.0 - fy) +
                            safeA(sx0, sy0 + 1) * (1.0 - fx) * fy +
                            safeA(sx0 + 1, sy0 + 1) * fx * fy;

                        const double invertedFill = 1.0 - shiftedAlpha;
                        const double mattedFill =
                            (origAlpha > 0.001)
                            ? invertedFill
                            : 0.0;
                        glowMask[y * out_w + x] = invertedFill * origAlpha; // pre-multiply: smooth glow di tepi semi-transparan
                        mask[y * out_w + x] = mattedFill;
                    }
                }

                // ── Pass 2: softness hanya pada rim mask ─────────────────────────
                ApplySoftness(mask, out_w, out_h, info.rim_softness, info.rim_softness);

                // ── Pass 3: composite ─────────────────────────────────────────
                for (A_long y = 0; y < out_h; ++y) {
                    for (A_long x = 0; x < out_w; ++x) {
                        const A_long ix = x + off_x;
                        const A_long iy = y + off_y;
                        if (ix < 0 || ix >= in_w || iy < 0 || iy >= in_h) continue;

                        const PF_Pixel8& sp = src_base[iy * src_stride + ix];
                        PF_Pixel8& dp = dst_base[y * dst_stride + x];

                        // FIX: copy src first; skip blend for fully-transparent pixels.
                        dp = sp;
                        if (sp.alpha == 0) continue;

                        const double rimAlpha = mask[y * out_w + x] * opacity;

                        if (rimAlpha > 0) {
                            dp.red = clamp255((int)(BlendChannel(sp.red / 255.0, rimR_n, rimAlpha, info.rim_blend_mode) * 255.0));
                            dp.green = clamp255((int)(BlendChannel(sp.green / 255.0, rimG_n, rimAlpha, info.rim_blend_mode) * 255.0));
                            dp.blue = clamp255((int)(BlendChannel(sp.blue / 255.0, rimB_n, rimAlpha, info.rim_blend_mode) * 255.0));
                            dp.alpha = sp.alpha;
                        }
                    }
                }

                // ── Pass 4: glow (uses un-matted glowMask) ──────────────────────
                const double ds = fmin(ds_x, ds_y);
                const double glowRX_8s = info.glow_individual ? info.glow_radius_x : info.glow_radius;
                const double glowRY_8s = info.glow_individual ? info.glow_radius_y : info.glow_radius;
                if (info.glow_intensity > 0.0 && info.glow_opacity > 0.0 && (glowRX_8s > 0.0 || glowRY_8s > 0.0)) {
                    const double glowR_dsX = glowRX_8s * (info.glow_individual ? ds_x : ds);
                    const double glowR_dsY = glowRY_8s * (info.glow_individual ? ds_y : ds);
                    double* glowBuf = BuildGlowBuffer(glowMask, out_w, out_h,
                        info.glow_threshold, info.glow_threshold_smooth, glowR_dsX, glowR_dsY);
                    if (glowBuf) {
                        const double glowR_n = info.glow_color.red / 255.0;
                        const double glowG_n = info.glow_color.green / 255.0;
                        const double glowB_n = info.glow_color.blue / 255.0;

                        for (A_long y = 0; y < out_h; ++y) {
                            for (A_long x = 0; x < out_w; ++x) {
                                PF_Pixel8& dp = dst_base[y * dst_stride + x];
                                // FIX: look up src alpha via origin offset — dp.alpha may
                                // have been modified by Pass 3, so we read the original.
                                const A_long ix = x + off_x;
                                const A_long iy = y + off_y;
                                const A_u_char srcAlpha8 = (ix >= 0 && ix < in_w && iy >= 0 && iy < in_h)
                                    ? src_base[iy * src_stride + ix].alpha : 0;

                                double g = glowBuf[y * out_w + x] * info.glow_intensity * info.glow_opacity;
                                if (g <= 0.0) continue;
                                if (g > 1.0) g = 1.0;

                                const A_u_char glowAlpha8 = clamp255((int)(g * 255.0));
                                if (glowAlpha8 == 0) continue;

                                if (srcAlpha8 == 0) {
                                    // Unpremult: full glow color, partial alpha → soft blend
                                    dp.red = clamp255((int)(glowR_n * 255.0));
                                    dp.green = clamp255((int)(glowG_n * 255.0));
                                    dp.blue = clamp255((int)(glowB_n * 255.0));
                                    dp.alpha = glowAlpha8;
                                }
                                else {
                                    // Inside source: attenuate agar outside bias dominan
                                    const double gIn = g * GLOW_INSIDE_ATTEN;
                                    if (gIn <= 0.0) continue;
                                    const double dR = dp.red / 255.0;
                                    const double dG = dp.green / 255.0;
                                    const double dB = dp.blue / 255.0;
                                    const double lum = dR * 0.2126 + dG * 0.7152 + dB * 0.0722;
                                    const double cR = glowR_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                                    const double cG = glowG_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;
                                    const double cB = glowB_n * (1.0 - info.glow_saturation) + lum * info.glow_saturation;

                                    dp.red = clamp255((int)(GlowBlendChannel(dR, cR * gIn, info.glow_type) * 255.0));
                                    dp.green = clamp255((int)(GlowBlendChannel(dG, cG * gIn, info.glow_type) * 255.0));
                                    dp.blue = clamp255((int)(GlowBlendChannel(dB, cB * gIn, info.glow_type) * 255.0));
                                    if (glowAlpha8 > dp.alpha) dp.alpha = glowAlpha8;
                                }
                            }
                        }
                        delete[] glowBuf;
                    }
                }

                delete[] glowMask;
                delete[] mask;
            }
        }

    checkin_params:
        PF_CHECKIN_PARAM(in_data, &colorParam);
        PF_CHECKIN_PARAM(in_data, &sizeParam);
        PF_CHECKIN_PARAM(in_data, &opacityParam);
        PF_CHECKIN_PARAM(in_data, &angleParam);
        PF_CHECKIN_PARAM(in_data, &softnessParam);
        PF_CHECKIN_PARAM(in_data, &blendParam);
        PF_CHECKIN_PARAM(in_data, &glowThreshParam);
        PF_CHECKIN_PARAM(in_data, &glowThreshSmoothParam);
        PF_CHECKIN_PARAM(in_data, &glowIndividualParam);
        PF_CHECKIN_PARAM(in_data, &glowRadiusParam);
        PF_CHECKIN_PARAM(in_data, &glowRadiusXParam);
        PF_CHECKIN_PARAM(in_data, &glowRadiusYParam);
        PF_CHECKIN_PARAM(in_data, &glowIntensityParam);
        PF_CHECKIN_PARAM(in_data, &glowSaturationParam);
        PF_CHECKIN_PARAM(in_data, &glowColorParam);
        PF_CHECKIN_PARAM(in_data, &glowOpacityParam);
        PF_CHECKIN_PARAM(in_data, &glowTypeParam);
    }

    if (input_world) extra->cb->checkin_layer_pixels(in_data->effect_ref, RIM_INPUT);
    // output world tidak perlu di-checkin

    return err;
}

// -----------------------------------------------------------------------------

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
        case PF_Cmd_UPDATE_PARAMS_UI:
            err = UpdateParamUI(in_data, out_data, params, output); break;
        case PF_Cmd_USER_CHANGED_PARAM:
            err = UpdateParamUI(in_data, out_data, params, output); break;
        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, output);      break;
        case PF_Cmd_SMART_PRE_RENDER:
            err = PreRender(in_data, out_data,
                reinterpret_cast<PF_PreRenderExtra*>(extra));     break;
        case PF_Cmd_SMART_RENDER:
            err = SmartRender(in_data, out_data,
                reinterpret_cast<PF_SmartRenderExtra*>(extra));   break;
        default: break;
        }
    }
    catch (PF_Err& thrown) { err = thrown; }
    return err;
}