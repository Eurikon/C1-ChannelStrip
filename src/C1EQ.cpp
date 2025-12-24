/*
 * C1EQ Foundation - Stereo Processing Implementation
 * Based on four-band example with memory-safe stereo conversion
 *
 * Memory Safety Strategy:
 * - Fixed-size arrays only (no std::vector)
 * - Stack-based processing buffers
 * - Safe VCV Rack lifecycle compliance
 */

#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include "EqAnalysisEngine.hpp"
#include <array>
#include <cmath>

using namespace rack;
using namespace simd;

namespace {

// C1 Custom Knob - 85% scaled TC encoder with graphite/anthracite palette
struct C1Knob : RoundKnob {
    C1Knob() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
        bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
    }
};

// C1 Knob with 280° rotation range (matching LED ring arc with 80° bottom gap)
struct C1Knob280 : RoundKnob {
    C1Knob280() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
        bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));

        // Symmetrical 280° rotation: -140° to +140°
        minAngle = -140.0f * (M_PI / 180.0f);  // -2.443 radians
        maxAngle = +140.0f * (M_PI / 180.0f);  // +2.443 radians
    }
};

// C1 Snap Knob - for discrete parameter selection
struct C1SnapKnob : C1Knob {
    C1SnapKnob() {
        snap = true;
    }
};

// C1 Snap Knob with 280° rotation
struct C1SnapKnob280 : C1Knob280 {
    C1SnapKnob280() {
        snap = true;
    }
};

// LED Ring Overlay Widget - 15 amber LEDs with 80° bottom gap
struct LedRingOverlay : widget::TransparentWidget {
    Module* module;
    int paramId;

    LedRingOverlay(Module* m, int pid) : module(m), paramId(pid) {
        box.size = Vec(50, 50); // Enlarged hit box around knob
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Get parameter value and normalize to 0.0 to 1.0 range
        ParamQuantity* pq = module->paramQuantities[paramId];
        float paramValue = pq ? pq->getScaledValue() : 0.0f;

        // LED ring specifications
        const int dotCount = 15;
        const float gapDeg = 80.0f;                      // 80° gap at bottom
        const float gap = gapDeg * (M_PI / 180.0f);     // Convert to radians
        const float start = -M_PI * 1.5f + gap * 0.5f;  // -230° start angle
        const float end   =  M_PI * 0.5f  - gap * 0.5f; // +50° end angle
        const float totalSpan = end - start;             // 280° arc span

        // Ring geometry (adjusted for 85% scaled knob = 24.095px diameter)
        const float knobRadius = 24.095f / 2.0f;         // 12.0475px knob radius
        const float ringOffset = 3.5f;                   // Distance from knob edge
        const float ringR = knobRadius + ringOffset;     // ~15.5px LED ring radius
        const float ledR = 0.9f;                         // LED dot radius

        // Center of drawing area
        const float cx = box.size.x / 2.0f;
        const float cy = box.size.y / 2.0f;

        // Calculate exact fractional position for smooth LED fading
        float exactPos = paramValue * (dotCount - 1);  // 0.0 to 14.0
        int led1 = (int)exactPos;                      // LED before (floor)
        int led2 = led1 + 1;                           // LED after
        float frac = exactPos - led1;                  // 0.0 to 1.0 between LEDs

        // Clamp LED indices
        led1 = clamp(led1, 0, dotCount - 1);
        led2 = clamp(led2, 0, dotCount - 1);

        // Draw 15 LED dots with smooth crossfade
        for (int i = 0; i < dotCount; ++i) {
            float t = (dotCount == 1) ? 0.0f : (float)i / (float)(dotCount - 1);
            float angle = start + t * totalSpan;
            float px = cx + ringR * std::cos(angle);
            float py = cy + ringR * std::sin(angle);

            // Calculate alpha with smooth fade between adjacent LEDs
            int alpha = 71;  // Dim (28% opacity)
            if (i == led1 && led1 != led2) {
                // LED1 fades out: bright → dim as frac goes 0 → 1
                alpha = 71 + (int)((230 - 71) * (1.0f - frac));
            } else if (i == led2 && led1 != led2) {
                // LED2 fades in: dim → bright as frac goes 0 → 1
                alpha = 71 + (int)((230 - 71) * frac);
            } else if (i == led1 && led1 == led2) {
                // At exact position (last LED or exact match)
                alpha = 230;
            }

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, px, py, ledR);
            nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, alpha));
            nvgFill(args.vg);
        }
    }
};

// LED Ring with 4 LEDs removed after first position (for discrete mode selector)
struct LedRingOverlaySkip4 : widget::TransparentWidget {
    Module* module;
    int paramId;

    LedRingOverlaySkip4(Module* m, int pid) : module(m), paramId(pid) {
        box.size = Vec(50, 50);
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        ParamQuantity* pq = module->paramQuantities[paramId];
        float paramValue = pq ? pq->getScaledValue() : 0.0f;

        // LED ring specifications (same as full ring)
        const int dotCount = 15;
        const float gapDeg = 80.0f;
        const float gap = gapDeg * (M_PI / 180.0f);
        const float start = -M_PI * 1.5f + gap * 0.5f;
        const float end   =  M_PI * 0.5f  - gap * 0.5f;
        const float totalSpan = end - start;

        const float knobRadius = 24.095f / 2.0f;
        const float ringOffset = 3.5f;
        const float ringR = knobRadius + ringOffset;
        const float ledR = 0.9f;

        const float cx = box.size.x / 2.0f;
        const float cy = box.size.y / 2.0f;

        int activeIndex = (int)std::round(paramValue * (dotCount - 1));
        activeIndex = std::max(0, std::min(dotCount - 1, activeIndex));

        NVGcolor dimAmber = nvgRGBA(0xFF, 0xAA, 0x33, 71);
        NVGcolor brightAmber = nvgRGBA(0xFF, 0xC0, 0x50, 230);

        // Draw LEDs with specific skip pattern for 4-mode selector
        // Keep: 0, 5, 9, 14 only
        for (int i = 0; i < dotCount; ++i) {
            // Skip positions 1-4, 6-8, 10-13
            if ((i >= 1 && i <= 4) || (i >= 6 && i <= 8) || (i >= 10 && i <= 13)) continue;

            float t = (dotCount == 1) ? 0.0f : (float)i / (float)(dotCount - 1);
            float angle = start + t * totalSpan;

            // Position adjustments: move position 5 by 5° counter-clockwise, position 9 by 5° clockwise
            if (i == 5) {
                angle -= 5.0f * (M_PI / 180.0f);  // -5° adjustment (counter-clockwise)
            } else if (i == 9) {
                angle += 5.0f * (M_PI / 180.0f);  // +5° adjustment (clockwise)
            }

            float px = cx + ringR * std::cos(angle);
            float py = cy + ringR * std::sin(angle);

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, px, py, ledR);
            nvgFillColor(args.vg, (i == activeIndex) ? brightAmber : dimAmber);
            nvgFill(args.vg);
        }
    }
};

// Memory-safe parameter smoother (stack-based)
struct SafeParamSmoother {
    double smoothed = 0.0;
    double tau_ms = 10.0;
    double sampleRate = 44100.0;

    void init(double sr, double initial = 0.0, double tau = 10.0) {
        sampleRate = sr > 0.0 ? sr : 44100.0;  // Safe fallback
        smoothed = initial;
        tau_ms = tau;
    }

    inline double process(double target) {
        double alpha = 1.0 - std::exp(-1000.0 / (tau_ms * sampleRate));
        smoothed += alpha * (target - smoothed);
        return smoothed;
    }

    inline void setImmediate(double v) { smoothed = v; }
};

// Memory-safe biquad filter
struct SafeBiquad {
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void reset() {
        x1 = x2 = y1 = y2 = 0.0;
    }

    inline double process(double in) {
        double out = (b0/a0)*in + (b1/a0)*x1 + (b2/a0)*x2 - (a1/a0)*y1 - (a2/a0)*y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }
};

// RBJ cookbook designs (unchanged - proven safe)
static void designPeaking(SafeBiquad &f, double fs, double f0, double Q, double gainDB) {
    if (f0 <= 0.0 || fs <= 0.0) {
        f.b0 = 1; f.b1 = 0; f.b2 = 0; f.a0 = 1; f.a1 = 0; f.a2 = 0;
        return;
    }

    double A = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * f0 / fs;
    double alpha = std::sin(w0) / (2.0 * Q);
    double cosw0 = std::cos(w0);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha / A;

    f.b0 = b0; f.b1 = b1; f.b2 = b2;
    f.a0 = a0; f.a1 = a1; f.a2 = a2;
}

static void designShelf(SafeBiquad &f, double fs, double f0, double S, double gainDB, bool highShelf) {
    if (f0 <= 0.0 || fs <= 0.0) {
        f.b0 = 1; f.b1 = 0; f.b2 = 0; f.a0 = 1; f.a1 = 0; f.a2 = 0;
        return;
    }

    double A = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * f0 / fs;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / 2.0 * std::sqrt((A + 1.0/A)*(1.0/S - 1.0) + 2.0);

    if (highShelf) {
        double b0 =    A*((A+1.0) + (A-1.0)*cosw0 + 2.0*sqrt(A)*alpha);
        double b1 = -2.0*A*((A-1.0) + (A+1.0)*cosw0);
        double b2 =    A*((A+1.0) + (A-1.0)*cosw0 - 2.0*sqrt(A)*alpha);
        double a0 =        (A+1.0) - (A-1.0)*cosw0 + 2.0*sqrt(A)*alpha;
        double a1 =  2.0*((A-1.0) - (A+1.0)*cosw0);
        double a2 =        (A+1.0) - (A-1.0)*cosw0 - 2.0*sqrt(A)*alpha;
        f.b0 = b0; f.b1 = b1; f.b2 = b2; f.a0 = a0; f.a1 = a1; f.a2 = a2;
    } else {
        double b0 =    A*((A+1.0) - (A-1.0)*cosw0 + 2.0*sqrt(A)*alpha);
        double b1 =  2.0*A*((A-1.0) - (A+1.0)*cosw0);
        double b2 =    A*((A+1.0) - (A-1.0)*cosw0 - 2.0*sqrt(A)*alpha);
        double a0 =        (A+1.0) + (A-1.0)*cosw0 + 2.0*sqrt(A)*alpha;
        double a1 = -2.0*((A-1.0) + (A+1.0)*cosw0);
        double a2 =        (A+1.0) + (A-1.0)*cosw0 - 2.0*sqrt(A)*alpha;
        f.b0 = b0; f.b1 = b1; f.b2 = b2; f.a0 = a0; f.a1 = a1; f.a2 = a2;
    }
}

// Analog character processor (Shelves-inspired techniques)
struct SafeAnalogProcessor {
    enum AnalogMode {
        TRANSPARENT = 0,  // Clean digital precision
        LIGHT = 1,        // Subtle harmonic enhancement
        MEDIUM = 2,       // Console-style saturation with VCA curves
        FULL = 3          // Complete circuit modeling with transformer coloration
    };

    double sampleRate = 44100.0;
    AnalogMode currentMode = TRANSPARENT;

    // State variables (Shelves-inspired)
    double vca_state = 0.0;
    double transformer_state_lp = 0.0;
    double transformer_state_hp = 0.0;
    double clip_detector_state = 0.0;  // Clipping indicator state

    // Shelves-inspired circuit constants
    static constexpr double kClampVoltage = 10.5;  // Op-amp saturation from Shelves
    static constexpr double kVCAGainConstant = -33e-3;  // 2164 VCA gain constant
    static constexpr double kClipThreshold = 7.0;   // Headroom threshold (3dB to ±10V limit)

    void init(double sr, AnalogMode mode = TRANSPARENT) {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        currentMode = mode;
        vca_state = 0.0;
        transformer_state_lp = transformer_state_hp = 0.0;
        clip_detector_state = 0.0;
    }

    void setMode(AnalogMode mode) {
        currentMode = mode;
    }

    inline double process(double input, bool vcaCompressionEnabled = true) {
        // Stage 1: VCA Compression (if enabled)
        double signal = vcaCompressionEnabled ? processVCACompression(input) : input;

        // Stage 2: Analog Character Modeling (based on mode)
        switch (currentMode) {
            case TRANSPARENT:
                // Pure transparent - no analog modeling, just clipping detection
                updateClippingDetector(signal);
                return signal;

            case LIGHT:
                return processSubtleHarmonics(signal);

            case MEDIUM:
                return processVCAColoration(signal);

            case FULL:
                return processFullCircuitModel(signal);

            default:
                return signal;
        }
    }

private:
    // VCA Compression Stage (separate from analog character modeling)
    inline double processVCACompression(double input) {
        // VCA gain behavior modeling (from Shelves VCA constant)
        const double vca_gain_constant = -33e-3;  // Shelves-inspired

        // Soft knee compression/saturation
        double abs_input = std::abs(input);
        double compressed = input;

        if (abs_input > 3.0) {  // Above ~3V start gentle compression
            double excess = abs_input - 3.0;
            double ratio = 1.0 / (1.0 + excess * 0.3);  // Soft knee
            compressed = input * ratio;
        }

        // Add subtle VCA state-dependent coloration
        vca_state = vca_state * 0.99 + abs_input * 0.01;  // Envelope following
        double vca_color = vca_state * vca_gain_constant * 0.1;

        return compressed + vca_color;
    }

    // Light mode: Subtle harmonic enhancement (2nd/3rd harmonics)
    inline double processSubtleHarmonics(double input) {
        // Gentle tanh saturation for 2nd/3rd harmonic content
        double drive = 1.2;
        double saturated = std::tanh(input * drive) / drive;

        // Mix with clean signal (75% clean, 25% harmonics)
        return 0.75 * input + 0.25 * saturated;
    }

    // Medium mode: VCA-style coloration (without compression - that's handled separately)
    inline double processVCAColoration(double input) {
        // VCA coloration modeling (from Shelves VCA constant)
        const double vca_gain_constant = -33e-3;  // Shelves-inspired

        double abs_input = std::abs(input);

        // Add VCA state-dependent coloration (no compression here)
        vca_state = vca_state * 0.99 + abs_input * 0.01;  // Envelope following
        double vca_color = vca_state * vca_gain_constant * 0.5;  // Increased from 0.2 for more coloration

        // Light saturation for console character
        double drive = 1.1;
        double saturated = std::tanh(input * drive) / drive;

        return input * 0.5 + saturated * 0.5 + vca_color;
    }

    // Full mode: Complete circuit modeling (VCA compression handled separately)
    inline double processFullCircuitModel(double input) {
        // Multi-stage analog emulation
        double signal = input;

        // Stage 1: Input transformer coloration
        signal = processTransformerColoration(signal);

        // Stage 2: VCA coloration (no compression - handled separately)
        double abs_input = std::abs(signal);
        vca_state = vca_state * 0.99 + abs_input * 0.01;  // Envelope following
        double vca_color = vca_state * kVCAGainConstant * 0.15;
        signal += vca_color;

        // Stage 3: Op-amp saturation with Shelves clamp voltage
        if (std::abs(signal) > kClampVoltage) {
            signal = (signal > 0) ? kClampVoltage : -kClampVoltage;
        }

        // Stage 4: Clipping detection (Shelves-inspired)
        updateClippingDetector(signal);

        // Stage 5: Output transformer with frequency response
        signal = processOutputTransformer(signal);

        return signal;
    }

    // Transformer coloration modeling
    inline double processTransformerColoration(double input) {
        // Simple transformer model: high-pass + low-pass for frequency response
        // Based on Shelves transformer behavior
        const double hp_cutoff = 20.0 / sampleRate;  // 20Hz highpass
        const double lp_cutoff = 15000.0 / sampleRate;  // 15kHz gentle rolloff

        // High-pass (DC blocking)
        transformer_state_hp = transformer_state_hp * (1.0 - hp_cutoff) + input * hp_cutoff;
        double hp_out = input - transformer_state_hp;

        // Low-pass (high frequency rolloff)
        transformer_state_lp += (hp_out - transformer_state_lp) * lp_cutoff;

        // Add subtle harmonic distortion
        double drive = 1.05;
        return std::tanh(transformer_state_lp * drive) / drive;
    }

public:
    // Clipping detection (Shelves-inspired)
    inline void updateClippingDetector(double signal) {
        // Clipping indicator with rise/fall time constants (from Shelves)
        const double kClipLEDRiseTime = 2e-3;   // 2ms rise time
        const double kClipLEDFallTime = 10e-3;  // 10ms fall time

        double abs_signal = std::abs(signal);
        bool clipping = abs_signal > kClipThreshold;

        if (clipping) {
            // Fast rise
            double alpha_rise = 1.0 - std::exp(-1.0 / (kClipLEDRiseTime * sampleRate));
            clip_detector_state += alpha_rise * (1.0 - clip_detector_state);
        } else {
            // Slow fall
            double alpha_fall = 1.0 - std::exp(-1.0 / (kClipLEDFallTime * sampleRate));
            clip_detector_state -= alpha_fall * clip_detector_state;
        }

        clip_detector_state = std::max(0.0, std::min(1.0, clip_detector_state));
    }

public:
    // Get clipping indicator state (0.0 to 1.0)
    inline double getClippingLevel() const {
        return clip_detector_state;
    }

    // Output transformer with saturation
    inline double processOutputTransformer(double input) {
        // Output transformer saturation curve
        double drive = 0.95;
        double saturated = std::tanh(input * drive) / drive;

        // Add transformer frequency response (subtle)
        const double transformer_color = 0.02;
        return input * (1.0 - transformer_color) + saturated * transformer_color;
    }
};

// Shelves oversampling factor calculation (copied from aafilter.hpp)
inline int SampleRateID(float sample_rate)
{
    if (768000 <= sample_rate) return 768000;
    else if (705600 <= sample_rate) return 705600;
    else if (384000 <= sample_rate) return 384000;
    else if (352800 <= sample_rate) return 352800;
    else if (192000 <= sample_rate) return 192000;
    else if (176400 <= sample_rate) return 176400;
    else if (96000 <= sample_rate) return 96000;
    else if (88200 <= sample_rate) return 88200;
    else if (48000 <= sample_rate) return 48000;
    else if (44100 <= sample_rate) return 44100;
    else if (24000 <= sample_rate) return 24000;
    else if (22050 <= sample_rate) return 22050;
    else if (12000 <= sample_rate) return 12000;
    else if (11025 <= sample_rate) return 11025;
    else if (8000 <= sample_rate) return 8000;
    else return 8000;
}

inline int OversamplingFactor(float sample_rate)
{
    switch (SampleRateID(sample_rate))
    {
    default:
    case 8000: return 15;
    case 11025: return 11;
    case 12000: return 10;
    case 22050: return 6;
    case 24000: return 5;
    case 44100: return 3;
    case 48000: return 3;
    case 88200: return 2;
    case 96000: return 2;
    case 176400: return 1;
    case 192000: return 1;
    case 352800: return 1;
    case 384000: return 1;
    case 705600: return 1;
    case 768000: return 1;
    }
}

// Shelves Anti-aliasing Filter Implementation (copied directly - no external dependencies)
static constexpr int kMaxNumSections = 8;

struct SOSCoefficients
{
    float b[3];
    float a[2];
};

template <typename T, int max_num_sections>
class SOSFilter
{
public:
    SOSFilter() : num_sections_(0), sections_{}, x_{}
    {
        Init(0);
    }

    SOSFilter(int num_sections) : num_sections_(0), sections_{}, x_{}
    {
        Init(num_sections);
    }

    void Init(int num_sections)
    {
        num_sections_ = num_sections;
        Reset();
    }

    void Init(int num_sections, const SOSCoefficients* sections)
    {
        num_sections_ = num_sections;
        Reset();
        SetCoefficients(sections);
    }

    void Reset()
    {
        for (int n = 0; n < num_sections_; n++)
        {
            x_[n][0] = 0.f;
            x_[n][1] = 0.f;
            x_[n][2] = 0.f;
        }

        x_[num_sections_][0] = 0.f;
        x_[num_sections_][1] = 0.f;
        x_[num_sections_][2] = 0.f;
    }

    void SetCoefficients(const SOSCoefficients* sections)
    {
        for (int n = 0; n < num_sections_; n++)
        {
            sections_[n].b[0] = sections[n].b[0];
            sections_[n].b[1] = sections[n].b[1];
            sections_[n].b[2] = sections[n].b[2];

            sections_[n].a[0] = sections[n].a[0];
            sections_[n].a[1] = sections[n].a[1];
        }
    }

    T Process(T in)
    {
        for (int n = 0; n < num_sections_; n++)
        {
            // Shift x state
            x_[n][2] = x_[n][1];
            x_[n][1] = x_[n][0];
            x_[n][0] = in;

            T out = 0.f;

            // Add x state
            out += sections_[n].b[0] * x_[n][0];
            out += sections_[n].b[1] * x_[n][1];
            out += sections_[n].b[2] * x_[n][2];

            // Subtract y state
            out -= sections_[n].a[0] * x_[n+1][0];
            out -= sections_[n].a[1] * x_[n+1][1];
            in = out;
        }

        // Shift final section x state
        x_[num_sections_][2] = x_[num_sections_][1];
        x_[num_sections_][1] = x_[num_sections_][0];
        x_[num_sections_][0] = in;

        return in;
    }

protected:
    int num_sections_;
    SOSCoefficients sections_[max_num_sections];
    T x_[max_num_sections + 1][3];
};

template <typename T>
class AAFilter
{
public:
    void Init(float sample_rate)
    {
        InitFilter(sample_rate);
    }

    T Process(T in)
    {
        return filter_.Process(in);
    }

protected:
    SOSFilter<T, kMaxNumSections> filter_;

    virtual void InitFilter(float sample_rate) = 0;
};

template <typename T>
class UpsamplingAAFilter : public AAFilter<T>
{
    void InitFilter(float sample_rate) override
    {
        switch (SampleRateID(sample_rate))
        {
        default:
        case 8000:
        {
            const SOSCoefficients kFilter8000x15[2] =
            {
                { {1.44208376e-04,  2.15422675e-04,  1.44208376e-04,  }, {-1.75298317e+00, 7.75007227e-01,  } },
                { {1.00000000e+00,  1.72189731e-01,  1.00000000e+00,  }, {-1.85199502e+00, 9.01687724e-01,  } },
            };
            AAFilter<T>::filter_.Init(2, kFilter8000x15);
            break;
        }
        case 11025:
        {
            const SOSCoefficients kFilter11025x11[2] =
            {
                { {3.47236726e-04,  5.94611382e-04,  3.47236726e-04,  }, {-1.66651262e+00, 7.05884392e-01,  } },
                { {1.00000000e+00,  7.58730216e-01,  1.00000000e+00,  }, {-1.77900341e+00, 8.69327961e-01,  } },
            };
            AAFilter<T>::filter_.Init(2, kFilter11025x11);
            break;
        }
        case 12000:
        {
            const SOSCoefficients kFilter12000x10[2] =
            {
                { {4.63786610e-04,  8.16220909e-04,  4.63786610e-04,  }, {-1.63450649e+00, 6.81471340e-01,  } },
                { {1.00000000e+00,  9.17818354e-01,  1.00000000e+00,  }, {-1.74936370e+00, 8.57701633e-01,  } },
            };
            AAFilter<T>::filter_.Init(2, kFilter12000x10);
            break;
        }
        case 22050:
        {
            const SOSCoefficients kFilter22050x6[3] =
            {
                { {1.95909107e-04,  3.07811266e-04,  1.95909107e-04,  }, {-1.58181808e+00, 6.40141057e-01,  } },
                { {1.00000000e+00,  1.34444168e-01,  1.00000000e+00,  }, {-1.58691814e+00, 7.40684153e-01,  } },
                { {1.00000000e+00,  -4.56209108e-01, 1.00000000e+00,  }, {-1.64635749e+00, 9.03421507e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter22050x6);
            break;
        }
        case 24000:
        {
            const SOSCoefficients kFilter24000x5[3] =
            {
                { {3.60375579e-04,  6.11714197e-04,  3.60375579e-04,  }, {-1.50089044e+00, 5.82797128e-01,  } },
                { {1.00000000e+00,  5.06808919e-01,  1.00000000e+00,  }, {-1.48367876e+00, 6.99513376e-01,  } },
                { {1.00000000e+00,  -8.08861216e-02, 1.00000000e+00,  }, {-1.52492835e+00, 8.87536413e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter24000x5);
            break;
        }
        case 44100:
        {
            const SOSCoefficients kFilter44100x3[4] =
            {
                { {6.47358611e-04,  1.15520581e-03,  6.47358611e-04,  }, {-1.35050917e+00, 4.84676642e-01,  } },
                { {1.00000000e+00,  7.82770646e-01,  1.00000000e+00,  }, {-1.24212580e+00, 6.01760550e-01,  } },
                { {1.00000000e+00,  9.46030879e-02,  1.00000000e+00,  }, {-1.12297856e+00, 7.63193697e-01,  } },
                { {1.00000000e+00,  -1.84341946e-01, 1.00000000e+00,  }, {-1.08165394e+00, 9.20980215e-01,  } },
            };
            AAFilter<T>::filter_.Init(4, kFilter44100x3);
            break;
        }
        case 48000:
        {
            const SOSCoefficients kFilter48000x3[4] =
            {
                { {4.56315687e-04,  7.94441994e-04,  4.56315687e-04,  }, {-1.40446545e+00, 5.18222739e-01,  } },
                { {1.00000000e+00,  6.11274299e-01,  1.00000000e+00,  }, {-1.31956356e+00, 6.25927896e-01,  } },
                { {1.00000000e+00,  -1.00659178e-01, 1.00000000e+00,  }, {-1.22823335e+00, 7.76420985e-01,  } },
                { {1.00000000e+00,  -3.75767056e-01, 1.00000000e+00,  }, {-1.20548228e+00, 9.25277956e-01,  } },
            };
            AAFilter<T>::filter_.Init(4, kFilter48000x3);
            break;
        }
        case 88200:
        {
            const SOSCoefficients kFilter88200x2[3] =
            {
                { {6.91751141e-04,  1.23689749e-03,  6.91751141e-04,  }, {-1.40714871e+00, 5.20902227e-01,  } },
                { {1.00000000e+00,  8.42431018e-01,  1.00000000e+00,  }, {-1.35717505e+00, 6.56002263e-01,  } },
                { {1.00000000e+00,  2.97097489e-01,  1.00000000e+00,  }, {-1.36759134e+00, 8.70920336e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter88200x2);
            break;
        }
        case 96000:
        {
            const SOSCoefficients kFilter96000x2[3] =
            {
                { {5.02504803e-04,  8.78421990e-04,  5.02504803e-04,  }, {-1.45413648e+00, 5.51330003e-01,  } },
                { {1.00000000e+00,  6.85942380e-01,  1.00000000e+00,  }, {-1.42143582e+00, 6.77242054e-01,  } },
                { {1.00000000e+00,  1.15756990e-01,  1.00000000e+00,  }, {-1.44850505e+00, 8.78995879e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter96000x2);
            break;
        }
        case 176400:
        {
            const SOSCoefficients kFilter176400x1[3] =
            {
                { {6.91751141e-04,  1.23689749e-03,  6.91751141e-04,  }, {-1.40714871e+00, 5.20902227e-01,  } },
                { {1.00000000e+00,  8.42431018e-01,  1.00000000e+00,  }, {-1.35717505e+00, 6.56002263e-01,  } },
                { {1.00000000e+00,  2.97097489e-01,  1.00000000e+00,  }, {-1.36759134e+00, 8.70920336e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter176400x1);
            break;
        }
        case 192000:
        {
            const SOSCoefficients kFilter192000x1[3] =
            {
                { {5.02504803e-04,  8.78421990e-04,  5.02504803e-04,  }, {-1.45413648e+00, 5.51330003e-01,  } },
                { {1.00000000e+00,  6.85942380e-01,  1.00000000e+00,  }, {-1.42143582e+00, 6.77242054e-01,  } },
                { {1.00000000e+00,  1.15756990e-01,  1.00000000e+00,  }, {-1.44850505e+00, 8.78995879e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter192000x1);
            break;
        }
        case 352800:
        {
            const SOSCoefficients kFilter352800x1[3] =
            {
                { {7.63562466e-05,  9.37911276e-05,  7.63562466e-05,  }, {-1.69760825e+00, 7.28764991e-01,  } },
                { {1.00000000e+00,  -5.40096033e-01, 1.00000000e+00,  }, {-1.72321786e+00, 8.05120281e-01,  } },
                { {1.00000000e+00,  -1.04012920e+00, 1.00000000e+00,  }, {-1.79287839e+00, 9.28245030e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter352800x1);
            break;
        }
        case 384000:
        {
            const SOSCoefficients kFilter384000x1[3] =
            {
                { {6.23104401e-05,  6.94740629e-05,  6.23104401e-05,  }, {-1.72153665e+00, 7.48079159e-01,  } },
                { {1.00000000e+00,  -6.96283878e-01, 1.00000000e+00,  }, {-1.74951535e+00, 8.19207305e-01,  } },
                { {1.00000000e+00,  -1.16050137e+00, 1.00000000e+00,  }, {-1.81879173e+00, 9.33631596e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter384000x1);
            break;
        }
        case 705600:
        {
            const SOSCoefficients kFilter705600x1[2] =
            {
                { {1.08339911e-04,  1.50243615e-04,  1.08339911e-04,  }, {-1.77824462e+00, 7.96098482e-01,  } },
                { {1.00000000e+00,  -5.03405956e-02, 1.00000000e+00,  }, {-1.87131112e+00, 9.11379528e-01,  } },
            };
            AAFilter<T>::filter_.Init(2, kFilter705600x1);
            break;
        }
        case 768000:
        {
            const SOSCoefficients kFilter768000x1[2] =
            {
                { {8.80491172e-05,  1.13851506e-04,  8.80491172e-05,  }, {-1.79584317e+00, 8.11038264e-01,  } },
                { {1.00000000e+00,  -2.19769620e-01, 1.00000000e+00,  }, {-1.88421935e+00, 9.18189356e-01,  } },
            };
            AAFilter<T>::filter_.Init(2, kFilter768000x1);
            break;
        }
        }
    }
};

template <typename T>
class DownsamplingAAFilter : public AAFilter<T>
{
    void InitFilter(float sample_rate) override
    {
        switch (SampleRateID(sample_rate))
        {
        default:
        case 8000:
        {
            const SOSCoefficients kFilter8000x15[8] =
            {
                { {1.27849152e-05,  -1.15294016e-05, 1.27849152e-05,  }, {-1.89076082e+00, 8.94920241e-01,  } },
                { {1.00000000e+00,  -1.81550212e+00, 1.00000000e+00,  }, {-1.90419428e+00, 9.15590704e-01,  } },
                { {1.00000000e+00,  -1.91311657e+00, 1.00000000e+00,  }, {-1.92211660e+00, 9.43157527e-01,  } },
                { {1.00000000e+00,  -1.93984732e+00, 1.00000000e+00,  }, {-1.93701740e+00, 9.66048056e-01,  } },
                { {1.00000000e+00,  -1.95004731e+00, 1.00000000e+00,  }, {-1.94692651e+00, 9.81207030e-01,  } },
                { {1.00000000e+00,  -1.95451979e+00, 1.00000000e+00,  }, {-1.95288929e+00, 9.90199673e-01,  } },
                { {1.00000000e+00,  -1.95654696e+00, 1.00000000e+00,  }, {-1.95649904e+00, 9.95393001e-01,  } },
                { {1.00000000e+00,  -1.95734415e+00, 1.00000000e+00,  }, {-1.95907829e+00, 9.98656952e-01,  } },
            };
            AAFilter<T>::filter_.Init(8, kFilter8000x15);
            break;
        }
        case 11025:
        {
            const SOSCoefficients kFilter11025x11[8] =
            {
                { {1.59399541e-05,  -5.45523304e-06, 1.59399541e-05,  }, {-1.85152256e+00, 8.59147179e-01,  } },
                { {1.00000000e+00,  -1.66827517e+00, 1.00000000e+00,  }, {-1.86567107e+00, 8.86607422e-01,  } },
                { {1.00000000e+00,  -1.84052903e+00, 1.00000000e+00,  }, {-1.88464921e+00, 9.23416484e-01,  } },
                { {1.00000000e+00,  -1.88895850e+00, 1.00000000e+00,  }, {-1.90052671e+00, 9.54145238e-01,  } },
                { {1.00000000e+00,  -1.90758521e+00, 1.00000000e+00,  }, {-1.91115958e+00, 9.74577353e-01,  } },
                { {1.00000000e+00,  -1.91577845e+00, 1.00000000e+00,  }, {-1.91763851e+00, 9.86729328e-01,  } },
                { {1.00000000e+00,  -1.91949726e+00, 1.00000000e+00,  }, {-1.92169110e+00, 9.93757870e-01,  } },
                { {1.00000000e+00,  -1.92096059e+00, 1.00000000e+00,  }, {-1.92481123e+00, 9.98179459e-01,  } },
            };
            AAFilter<T>::filter_.Init(8, kFilter11025x11);
            break;
        }
        case 12000:
        {
            const SOSCoefficients kFilter12000x10[8] =
            {
                { {1.74724987e-05,  -2.65793181e-06, 1.74724987e-05,  }, {-1.83684224e+00, 8.46022748e-01,  } },
                { {1.00000000e+00,  -1.60455772e+00, 1.00000000e+00,  }, {-1.85073181e+00, 8.75957566e-01,  } },
                { {1.00000000e+00,  -1.80816772e+00, 1.00000000e+00,  }, {-1.86939499e+00, 9.16147406e-01,  } },
                { {1.00000000e+00,  -1.86608225e+00, 1.00000000e+00,  }, {-1.88504252e+00, 9.49754529e-01,  } },
                { {1.00000000e+00,  -1.88843627e+00, 1.00000000e+00,  }, {-1.89555097e+00, 9.72128817e-01,  } },
                { {1.00000000e+00,  -1.89828300e+00, 1.00000000e+00,  }, {-1.90199243e+00, 9.85446639e-01,  } },
                { {1.00000000e+00,  -1.90275515e+00, 1.00000000e+00,  }, {-1.90608719e+00, 9.93153182e-01,  } },
                { {1.00000000e+00,  -1.90451538e+00, 1.00000000e+00,  }, {-1.90935079e+00, 9.98002792e-01,  } },
            };
            AAFilter<T>::filter_.Init(8, kFilter12000x10);
            break;
        }
        case 22050:
        {
            const SOSCoefficients kFilter22050x6[8] =
            {
                { {3.67003458e-05,  3.08516252e-05,  3.67003458e-05,  }, {-1.72921734e+00, 7.53994379e-01,  } },
                { {1.00000000e+00,  -1.04633213e+00, 1.00000000e+00,  }, {-1.73301180e+00, 8.01279004e-01,  } },
                { {1.00000000e+00,  -1.49728136e+00, 1.00000000e+00,  }, {-1.73817883e+00, 8.65169236e-01,  } },
                { {1.00000000e+00,  -1.64018498e+00, 1.00000000e+00,  }, {-1.74263646e+00, 9.18956353e-01,  } },
                { {1.00000000e+00,  -1.69729414e+00, 1.00000000e+00,  }, {-1.74585766e+00, 9.54949897e-01,  } },
                { {1.00000000e+00,  -1.72280865e+00, 1.00000000e+00,  }, {-1.74827060e+00, 9.76444779e-01,  } },
                { {1.00000000e+00,  -1.73447030e+00, 1.00000000e+00,  }, {-1.75063420e+00, 9.88907702e-01,  } },
                { {1.00000000e+00,  -1.73907302e+00, 1.00000000e+00,  }, {-1.75392950e+00, 9.96761482e-01,  } },
            };
            AAFilter<T>::filter_.Init(8, kFilter22050x6);
            break;
        }
        case 24000:
        {
            const SOSCoefficients kFilter24000x5[8] =
            {
                { {5.41421251e-05,  6.11551260e-05,  5.41421251e-05,  }, {-1.67503641e+00, 7.10371798e-01,  } },
                { {1.00000000e+00,  -7.40935436e-01, 1.00000000e+00,  }, {-1.66871015e+00, 7.66060345e-01,  } },
                { {1.00000000e+00,  -1.30326567e+00, 1.00000000e+00,  }, {-1.66021936e+00, 8.41290550e-01,  } },
                { {1.00000000e+00,  -1.49333046e+00, 1.00000000e+00,  }, {-1.65322192e+00, 9.04610823e-01,  } },
                { {1.00000000e+00,  -1.57100117e+00, 1.00000000e+00,  }, {-1.64887008e+00, 9.46976897e-01,  } },
                { {1.00000000e+00,  -1.60602637e+00, 1.00000000e+00,  }, {-1.64694927e+00, 9.72274830e-01,  } },
                { {1.00000000e+00,  -1.62210241e+00, 1.00000000e+00,  }, {-1.64717215e+00, 9.86942309e-01,  } },
                { {1.00000000e+00,  -1.62845914e+00, 1.00000000e+00,  }, {-1.64981608e+00, 9.96186562e-01,  } },
            };
            AAFilter<T>::filter_.Init(8, kFilter24000x5);
            break;
        }
        case 44100:
        {
            const SOSCoefficients kFilter44100x3[6] =
            {
                { {2.68627470e-04,  4.49235868e-04,  2.68627470e-04,  }, {-1.45093297e+00, 5.48077112e-01,  } },
                { {1.00000000e+00,  3.56445341e-01,  1.00000000e+00,  }, {-1.37442858e+00, 6.39226382e-01,  } },
                { {1.00000000e+00,  -4.09182122e-01, 1.00000000e+00,  }, {-1.27479281e+00, 7.60081618e-01,  } },
                { {1.00000000e+00,  -7.45642800e-01, 1.00000000e+00,  }, {-1.19642609e+00, 8.60924455e-01,  } },
                { {1.00000000e+00,  -8.92243997e-01, 1.00000000e+00,  }, {-1.15251661e+00, 9.30694207e-01,  } },
                { {1.00000000e+00,  -9.48436919e-01, 1.00000000e+00,  }, {-1.14204907e+00, 9.79130351e-01,  } },
            };
            AAFilter<T>::filter_.Init(6, kFilter44100x3);
            break;
        }
        case 48000:
        {
            const SOSCoefficients kFilter48000x3[5] =
            {
                { {2.57287527e-04,  4.26397322e-04,  2.57287527e-04,  }, {-1.46657488e+00, 5.58547936e-01,  } },
                { {1.00000000e+00,  3.12318565e-01,  1.00000000e+00,  }, {-1.39841450e+00, 6.48946069e-01,  } },
                { {1.00000000e+00,  -4.43959552e-01, 1.00000000e+00,  }, {-1.31299240e+00, 7.70865691e-01,  } },
                { {1.00000000e+00,  -7.61106497e-01, 1.00000000e+00,  }, {-1.25520703e+00, 8.77567308e-01,  } },
                { {1.00000000e+00,  -8.77468526e-01, 1.00000000e+00,  }, {-1.24463600e+00, 9.61716067e-01,  } },
            };
            AAFilter<T>::filter_.Init(5, kFilter48000x3);
            break;
        }
        case 88200:
        {
            const SOSCoefficients kFilter88200x2[3] =
            {
                { {6.91751141e-04,  1.23689749e-03,  6.91751141e-04,  }, {-1.40714871e+00, 5.20902227e-01,  } },
                { {1.00000000e+00,  8.42431018e-01,  1.00000000e+00,  }, {-1.35717505e+00, 6.56002263e-01,  } },
                { {1.00000000e+00,  2.97097489e-01,  1.00000000e+00,  }, {-1.36759134e+00, 8.70920336e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter88200x2);
            break;
        }
        case 96000:
        {
            const SOSCoefficients kFilter96000x2[3] =
            {
                { {5.02504803e-04,  8.78421990e-04,  5.02504803e-04,  }, {-1.45413648e+00, 5.51330003e-01,  } },
                { {1.00000000e+00,  6.85942380e-01,  1.00000000e+00,  }, {-1.42143582e+00, 6.77242054e-01,  } },
                { {1.00000000e+00,  1.15756990e-01,  1.00000000e+00,  }, {-1.44850505e+00, 8.78995879e-01,  } },
            };
            AAFilter<T>::filter_.Init(3, kFilter96000x2);
            break;
        }
        case 176400:
        {
            const SOSCoefficients kFilter176400x1[1] =
            {
                { {1.95938020e-01,  3.91858763e-01,  1.95938020e-01,  }, {-4.62313019e-01, 2.46047822e-01,  } },
            };
            AAFilter<T>::filter_.Init(1, kFilter176400x1);
            break;
        }
        case 192000:
        {
            const SOSCoefficients kFilter192000x1[1] =
            {
                { {1.74603587e-01,  3.49188678e-01,  1.74603587e-01,  }, {-5.65216145e-01, 2.63611998e-01,  } },
            };
            AAFilter<T>::filter_.Init(1, kFilter192000x1);
            break;
        }
        case 352800:
        {
            const SOSCoefficients kFilter352800x1[1] =
            {
                { {6.99874107e-02,  1.39948456e-01,  6.99874107e-02,  }, {-1.16347041e+00, 4.43393682e-01,  } },
            };
            AAFilter<T>::filter_.Init(1, kFilter352800x1);
            break;
        }
        case 384000:
        {
            const SOSCoefficients kFilter384000x1[1] =
            {
                { {6.09620331e-02,  1.21896769e-01,  6.09620331e-02,  }, {-1.22760212e+00, 4.71422957e-01,  } },
            };
            AAFilter<T>::filter_.Init(1, kFilter384000x1);
            break;
        }
        case 705600:
        {
            const SOSCoefficients kFilter705600x1[1] =
            {
                { {2.13438638e-02,  4.26550556e-02,  2.13438638e-02,  }, {-1.57253460e+00, 6.57877382e-01,  } },
            };
            AAFilter<T>::filter_.Init(1, kFilter705600x1);
            break;
        }
        case 768000:
        {
            const SOSCoefficients kFilter768000x1[1] =
            {
                { {1.83197956e-02,  3.66063440e-02,  1.83197956e-02,  }, {-1.60702602e+00, 6.80271956e-01,  } },
            };
            AAFilter<T>::filter_.Init(1, kFilter768000x1);
            break;
        }
        }
    }
};

// Sophisticated 2x oversampler with FIR anti-aliasing filters (NO std::vector)
struct SafeOversampler2x {
    static constexpr int FILTER_ORDER = 8;
    static constexpr int HISTORY_SIZE = FILTER_ORDER + 1;

    double sampleRate = 44100.0;

    // Stack-based filter histories for stereo processing
    double upsampleHistoryL[HISTORY_SIZE] = {};
    double upsampleHistoryR[HISTORY_SIZE] = {};
    double downsampleHistoryL[HISTORY_SIZE] = {};
    double downsampleHistoryR[HISTORY_SIZE] = {};

    // Half-band FIR coefficients for anti-aliasing (odd coefficients are zero)
    static constexpr double halfbandCoeffs[FILTER_ORDER + 1] = {
        -0.0096189, 0.0000000, 0.0632810, 0.0000000, -0.3789654,
        0.6308904, -0.3789654, 0.0000000, 0.0632810
    };

    void init(double sr) {
        sampleRate = sr > 0.0 ? sr : 44100.0;

        // Clear all histories
        for (int i = 0; i < HISTORY_SIZE; i++) {
            upsampleHistoryL[i] = upsampleHistoryR[i] = 0.0;
            downsampleHistoryL[i] = downsampleHistoryR[i] = 0.0;
        }
    }

    // Stereo upsample with sophisticated FIR anti-aliasing
    inline void upsampleStereo(double inL, double inR, double samples[4]) {
        // Shift histories
        for (int i = HISTORY_SIZE - 1; i > 0; i--) {
            upsampleHistoryL[i] = upsampleHistoryL[i - 1];
            upsampleHistoryR[i] = upsampleHistoryR[i - 1];
        }
        upsampleHistoryL[0] = inL;
        upsampleHistoryR[0] = inR;

        // Generate two phases (0 and π) for 2x oversampling
        // Phase 0: Direct sample
        samples[0] = applyHalfbandFilter(upsampleHistoryL) * 2.0;  // Compensate for 0.5 gain
        samples[1] = applyHalfbandFilter(upsampleHistoryR) * 2.0;

        // Phase π: Zero-stuff and filter for interpolated sample
        // Insert zero in history for zero-stuffing
        for (int i = HISTORY_SIZE - 1; i > 0; i--) {
            upsampleHistoryL[i] = upsampleHistoryL[i - 1];
            upsampleHistoryR[i] = upsampleHistoryR[i - 1];
        }
        upsampleHistoryL[0] = 0.0;
        upsampleHistoryR[0] = 0.0;

        samples[2] = applyHalfbandFilter(upsampleHistoryL) * 2.0;
        samples[3] = applyHalfbandFilter(upsampleHistoryR) * 2.0;
    }

    // Stereo downsample with anti-aliasing
    inline void downsampleStereo(const double samples[4], double &outL, double &outR) {
        // Process both oversampled pairs through anti-aliasing filter
        double filteredL = 0.0, filteredR = 0.0;

        // Process first pair (samples[0], samples[1])
        for (int i = HISTORY_SIZE - 1; i > 0; i--) {
            downsampleHistoryL[i] = downsampleHistoryL[i - 1];
            downsampleHistoryR[i] = downsampleHistoryR[i - 1];
        }
        downsampleHistoryL[0] = samples[0];
        downsampleHistoryR[0] = samples[1];

        filteredL += applyHalfbandFilter(downsampleHistoryL);
        filteredR += applyHalfbandFilter(downsampleHistoryR);

        // Process second pair (samples[2], samples[3])
        for (int i = HISTORY_SIZE - 1; i > 0; i--) {
            downsampleHistoryL[i] = downsampleHistoryL[i - 1];
            downsampleHistoryR[i] = downsampleHistoryR[i - 1];
        }
        downsampleHistoryL[0] = samples[2];
        downsampleHistoryR[0] = samples[3];

        filteredL += applyHalfbandFilter(downsampleHistoryL);
        filteredR += applyHalfbandFilter(downsampleHistoryR);

        // Average and apply proper scaling
        outL = filteredL * 0.5;  // Average of two phases
        outR = filteredR * 0.5;
    }

private:
    // Apply halfband FIR filter to history buffer
    inline double applyHalfbandFilter(const double history[HISTORY_SIZE]) {
        double output = 0.0;
        for (int i = 0; i < HISTORY_SIZE; i++) {
            output += history[i] * halfbandCoeffs[i];
        }
        return output;
    }
};

// ---------------------- C1EQ Module ----------------------

// Custom ParamQuantity for mode switches with text labels
struct ModeParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int modeValue = (int)std::round(getValue());
        switch (modeValue) {
            case 0: return "Shelf";
            case 1: return "Bell";
            case 2: return "Cut";
            default: return ParamQuantity::getDisplayValueString();
        }
    }
};

// Custom ParamQuantity for Bypass button with ON/OFF labels
struct BypassParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "ON";
    }
};

struct C1EQ : Module {
    enum ParamIds {
        GLOBAL_GAIN_PARAM,
        // 4 parametric bands: Freq, Q, Gain per band
        // Note: B1 and B4 have hardcoded Q values (no Q params)
        B1_FREQ_PARAM, B1_GAIN_PARAM,
        B2_FREQ_PARAM, B2_Q_PARAM, B2_GAIN_PARAM,
        B3_FREQ_PARAM, B3_Q_PARAM, B3_GAIN_PARAM,
        B4_FREQ_PARAM, B4_GAIN_PARAM,
        // Band 1 & 4 mode toggles (Console1 hardware design)
        B1_MODE_PARAM,  // 3-way: cut(0) - bell(1) - shelf(2)
        B4_MODE_PARAM,  // 3-way: cut(0) - bell(1) - shelf(2)
        // Controls
        OVERSAMPLE_PARAM,
        BYPASS_PARAM,
        ANALOG_MODE_PARAM,  // 4-position analog character selector
        ANALYSER_ENABLE_PARAM,  // Enable/disable spectrum analyser
        NUM_PARAMS
    };

    enum InputIds {
        AUDIO_INPUT_L, AUDIO_INPUT_R,
        NUM_INPUTS
    };

    enum OutputIds {
        AUDIO_OUTPUT_L, AUDIO_OUTPUT_R,
        NUM_OUTPUTS
    };

    enum LightIds {
        BYPASS_LIGHT,
        ENUMS(ANALOG_LIGHT, 3),  // RGB analog processing indicator
        ENUMS(CLIP_LIGHT, 3),    // RGB clipping indicator (Shelves-inspired)
        OVERSAMPLE_LIGHT,        // Oversampling status indicator
        ENUMS(B1_MODE_LIGHT, 3), // LF mode lights (3 positions)
        ENUMS(B4_MODE_LIGHT, 3), // HF mode lights (3 positions)
        B1_MODE_BUTTON_LIGHT,    // LF mode button light
        B4_MODE_BUTTON_LIGHT,    // HF mode button light
        NUM_LIGHTS
    };

    // SIMD-optimized stereo DSP objects (Bandit pattern)
    dsp::TBiquadFilter<float_4> bands[4][2];     // [band][L/R] - dual stereo as float_4 SIMD

    // SAFE: Parameter smoothers (shared for stereo-linked processing)
    SafeParamSmoother freqSmoothers[4];
    SafeParamSmoother qSmoothers[4];
    SafeParamSmoother gainSmoothers[4];
    SafeParamSmoother globalGainSmoother;

    // SAFE: Coefficient caches (fixed arrays)
    struct BandCache {
        double f0 = -1, Q = -1, g = -1000;
        int mode = -1;  // Include mode in cache
        double sampleRate = -1;  // Include sample rate in cache (critical for oversampling)
    };
    BandCache bandCache[4];

    // Oversampling (Shelves approach)
    static constexpr int OVERSAMPLING_FACTOR = 4;
    int oversampling_ = OVERSAMPLING_FACTOR;

    // Shelves anti-aliasing filters (copied exact structure)
    UpsamplingAAFilter<float> up_filter_[3];      // 3 upsampling filters
    DownsamplingAAFilter<float> down_filter_[2];  // 2 downsampling filters

    // SAFE: Analog character processors (stereo)
    SafeAnalogProcessor analogProcessorL, analogProcessorR;

    // Real oversampling implementation
    SafeOversampler2x oversampler;

    // VCA compression control (context menu)
    bool vcaCompressionEnabled = false;  // Default: disabled (user must enable)
    bool enableProportionalQ = true;   // Default: enabled for musical response

    // Spectrum analysis for display
    EqAnalysisEngine* spectrumAnalyzer = nullptr;
    std::atomic<bool> isShuttingDown{false};  // Thread safety: prevent access during destruction

    // Analyzer auto-shutdown state
    float analyzerIdleTimer = 0.0f;      // Counts time since display OFF
    bool analyzerDSPActive = true;        // Track worker thread state

    dsp::ClockDivider lightDivider;       // LED update clock divider (update every 256 samples)

    // Coefficient update clock divider (update every 16 samples for efficiency)
    int coefficientDivider = 0;

    // Event-based mode tracking for Cut mode gain lock
    float lastB1Mode = -1.0f;
    float lastB4Mode = -1.0f;
    bool b1GainLocked = false;
    bool b4GainLocked = false;

    C1EQ() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        // Global controls
        configParam(GLOBAL_GAIN_PARAM, -24.f, 24.f, 0.f, "Master Gain", " dB");
        configParam<BypassParamQuantity>(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass");
        configParam(OVERSAMPLE_PARAM, 0.f, 1.f, 0.f, "Oversample");
        configParam(ANALOG_MODE_PARAM, 0.f, 3.f, 0.f, "Analog Character Mode");
        getParamQuantity(ANALOG_MODE_PARAM)->snapEnabled = true;  // Stepped values
        configParam(ANALYSER_ENABLE_PARAM, 0.f, 1.f, 1.f, "Spectrum Analyser");  // Default ON

        // 4 Parametric bands with frequency allocation
        // Band 1 (Low): 20Hz - 400Hz, default 20Hz (hardcoded Q=0.8)
        configParam(B1_FREQ_PARAM, std::log2(20.0f), std::log2(400.0f), std::log2(20.0f), "Low Freq", " Hz", 2.0f);
        configParam(B1_GAIN_PARAM, -20.f, 20.f, 0.f, "Low Gain", " dB");
        configParam<ModeParamQuantity>(B1_MODE_PARAM, 0.f, 2.f, 2.f, "Low Mode");
        getParamQuantity(B1_MODE_PARAM)->snapEnabled = true;

        // Band 2 (Low-Mid): 200Hz - 2kHz, default 250Hz
        configParam(B2_FREQ_PARAM, std::log2(200.0f), std::log2(2000.0f), std::log2(250.0f), "Low-Mid Freq", " Hz", 2.0f);
        configParam(B2_Q_PARAM, 0.3f, 12.f, 1.0f, "Low-Mid Q");
        configParam(B2_GAIN_PARAM, -20.f, 20.f, 0.f, "Low-Mid Gain", " dB");

        // Band 3 (High-Mid): 1kHz - 8kHz, default 2kHz
        configParam(B3_FREQ_PARAM, std::log2(1000.0f), std::log2(8000.0f), std::log2(2000.0f), "High-Mid Freq", " Hz", 2.0f);
        configParam(B3_Q_PARAM, 0.3f, 12.f, 1.0f, "High-Mid Q");
        configParam(B3_GAIN_PARAM, -20.f, 20.f, 0.f, "High-Mid Gain", " dB");

        // Band 4 (High): 4kHz - 20kHz, default 20kHz (hardcoded Q=1.0)
        configParam(B4_FREQ_PARAM, std::log2(4000.0f), std::log2(20000.0f), std::log2(20000.0f), "High Freq", " Hz", 2.0f);
        configParam(B4_GAIN_PARAM, -20.f, 20.f, 0.f, "High Gain", " dB");
        configParam<ModeParamQuantity>(B4_MODE_PARAM, 0.f, 2.f, 2.f, "High Mode");
        getParamQuantity(B4_MODE_PARAM)->snapEnabled = true;

        // Configure inputs/outputs
        configInput(AUDIO_INPUT_L, "Audio Left");
        configInput(AUDIO_INPUT_R, "Audio Right");
        configOutput(AUDIO_OUTPUT_L, "Audio Left");
        configOutput(AUDIO_OUTPUT_R, "Audio Right");

        // VCV Rack engine-level bypass (right-click menu)
        configBypass(AUDIO_INPUT_L, AUDIO_OUTPUT_L);
        configBypass(AUDIO_INPUT_R, AUDIO_OUTPUT_R);

        lightDivider.setDivision(256);  // Update LEDs every 256 samples (187.5Hz at 48kHz)
    }

    void onRandomize(const RandomizeEvent& e) override {
        (void)e;  // Suppress unused parameter warning
        // Disable randomize - do nothing
    }

    void onReset() override {
        // Reset all parameters to defaults
        Module::onReset();

        // Reset context menu settings to defaults
        vcaCompressionEnabled = false;  // Off (default disabled)
        enableProportionalQ = true;     // On (matches default at line 1194)
    }

    ~C1EQ() {
        // Signal shutdown and clean up spectrum analyzer
        isShuttingDown.store(true);
        if (spectrumAnalyzer) {
            delete spectrumAnalyzer;
            spectrumAnalyzer = nullptr;
        }
    }

    void onSampleRateChange() override {
        double sr = APP->engine->getSampleRate();
        if (sr <= 0.0) sr = 44100.0;  // Safe fallback

        // Initialize smoothers
        for (int i = 0; i < 4; ++i) {
            freqSmoothers[i].init(sr, 1000.0, 6.0);   // 6ms
            qSmoothers[i].init(sr, 1.0, 25.0);        // 25ms
            gainSmoothers[i].init(sr, 0.0, 20.0);     // 20ms
        }
        globalGainSmoother.init(sr, 0.0, 50.0);       // 50ms

        // Shelves oversampling approach
        oversampling_ = OversamplingFactor(sr);

        // Initialize Shelves anti-aliasing filters
        up_filter_[0].Init(sr);
        up_filter_[1].Init(sr);
        up_filter_[2].Init(sr);
        down_filter_[0].Init(sr);
        down_filter_[1].Init(sr);

        // Initialize analog processors
        analogProcessorL.init(sr, SafeAnalogProcessor::TRANSPARENT);
        analogProcessorR.init(sr, SafeAnalogProcessor::TRANSPARENT);

        // Initialize oversampler
        oversampler.init(sr);

        // Reset analyzer auto-shutdown state
        analyzerIdleTimer = 0.0f;
        analyzerDSPActive = true;

        // Force coefficient update on first sample after initialization
        coefficientDivider = 15;  // Will trigger update on next process() call

        // Reset SIMD filter states and caches
        for (int i = 0; i < 4; ++i) {
            bands[i][0].reset();
            bands[i][1].reset();
            bandCache[i].f0 = -1;
            bandCache[i].Q = -1;
            bandCache[i].g = -1000;
            bandCache[i].mode = -1;
        }
    }

    void updateBandCoefficients(int band, double sampleRate) {
        if (band < 0 || band >= 4) return;

        // MindMeld-style parameter smoothing - use existing smoothers to prevent artifacts
        // Get correct parameter indices for each band
        ParamIds freqParam, qParam, gainParam;
        switch(band) {
            case 0: freqParam = B1_FREQ_PARAM; gainParam = B1_GAIN_PARAM; break;
            case 1: freqParam = B2_FREQ_PARAM; qParam = B2_Q_PARAM; gainParam = B2_GAIN_PARAM; break;
            case 2: freqParam = B3_FREQ_PARAM; qParam = B3_Q_PARAM; gainParam = B3_GAIN_PARAM; break;
            case 3: freqParam = B4_FREQ_PARAM; gainParam = B4_GAIN_PARAM; break;
            default: return;
        }

        // Convert logarithmic frequency parameter back to Hz for processing
        double f0_raw = params[freqParam].getValue();
        double f0 = freqSmoothers[band].process(std::pow(2.0, f0_raw));  // Convert log2 back to linear Hz

        // Hardcoded Q values for Console1 hardware compatibility (bands 1 & 4 have no Q encoders)
        double Q;
        if (band == 0) {        // Band 1 (LF)
            Q = qSmoothers[band].process(0.8);  // Hardcoded Q=0.8 for LF band
        } else if (band == 3) { // Band 4 (HF)
            Q = qSmoothers[band].process(1.0);  // Hardcoded Q=1.0 for HF band
        } else {                // Bands 2 & 3 use parameter knobs
            Q = qSmoothers[band].process(params[qParam].getValue());
        }

        double gain = gainSmoothers[band].process(params[gainParam].getValue());

        // Band mode handling for Bands 1 & 4 (Console1 hardware design)
        int mode = 1;  // Default: bell mode for bands 2 & 3
        if (band == 0) {  // Band 1 (Low)
            int rawMode = (int)std::round(params[B1_MODE_PARAM].getValue());
            mode = 2 - rawMode;  // Invert: 0->2(shelf), 1->1(bell), 2->0(cut)
        } else if (band == 3) {  // Band 4 (High)
            int rawMode = (int)std::round(params[B4_MODE_PARAM].getValue());
            mode = 2 - rawMode;  // Invert: 0->2(shelf), 1->1(bell), 2->0(cut)
        }

        // Handle cut mode - HPF for LF band, LPF for HF band
        if (mode == 0) {  // Cut mode
            // Fixed Q at 0.707 (Butterworth response), gain ignored (V=1.0)
            float fc = f0 / sampleRate;
            float cutQ = 0.707f;  // Butterworth (maximally flat passband)
            float cutV = 1.0f;     // No gain adjustment in Cut mode

            // Determine filter type based on band
            dsp::TBiquadFilter<float_4>::Type cutFilterType;
            if (band == 0) {  // LF band = High-pass (removes low frequencies)
                cutFilterType = dsp::TBiquadFilter<float_4>::HIGHPASS;
            } else if (band == 3) {  // HF band = Low-pass (removes high frequencies)
                cutFilterType = dsp::TBiquadFilter<float_4>::LOWPASS;
            } else {
                // Bands 2 & 3 (mid bands) don't have Cut mode - this shouldn't happen
                // Bypass if somehow triggered
                bands[band][0].setParameters(dsp::TBiquadFilter<float_4>::PEAK, 0.25f, 1.0f, 1.0f);
                bands[band][1].setParameters(dsp::TBiquadFilter<float_4>::PEAK, 0.25f, 1.0f, 1.0f);
                return;
            }

            // Configure Cut mode filters with caching
            if (std::abs(bandCache[band].f0 - f0) > 1e-6 ||
                std::abs(bandCache[band].Q - cutQ) > 1e-4 ||
                bandCache[band].mode != mode ||
                std::abs(bandCache[band].sampleRate - sampleRate) > 1.0) {

                bands[band][0].setParameters(cutFilterType, fc, cutQ, cutV);
                bands[band][1].setParameters(cutFilterType, fc, cutQ, cutV);

                bandCache[band].f0 = f0;
                bandCache[band].Q = cutQ;
                bandCache[band].g = 0.0;  // Gain not used in Cut mode
                bandCache[band].mode = mode;
                bandCache[band].sampleRate = sampleRate;
            }
            return;
        }

        // Proportional Q behavior (from four-band example) - now optional
        double Qeff = enableProportionalQ ? Q * (1.0 + 0.02 * std::abs(gain)) : Q;

        // Check cache and redesign if needed (including mode changes and sample rate)
        const double EPS_F = 1e-6;
        if (std::abs(bandCache[band].f0 - f0) > EPS_F ||
            std::abs(bandCache[band].Q - Qeff) > 1e-4 ||
            std::abs(bandCache[band].g - gain) > 1e-4 ||
            bandCache[band].mode != mode ||
            std::abs(bandCache[band].sampleRate - sampleRate) > 1.0) {  // Sample rate changed (oversampling toggle)

            // SIMD filter setup: normalized frequency and V parameter
            float fc = f0 / sampleRate;
            float V = std::pow(10.0f, gain / 40.0f);

            // Configure filter type based on mode
            dsp::TBiquadFilter<float_4>::Type filterType;
            if (mode == 1) {  // Bell mode (peaking)
                filterType = dsp::TBiquadFilter<float_4>::PEAK;
            } else if (mode == 2) {  // Shelf mode
                if (band == 0) {  // Band 1 = Low shelf
                    filterType = dsp::TBiquadFilter<float_4>::LOWSHELF;
                } else {  // Band 4 = High shelf
                    filterType = dsp::TBiquadFilter<float_4>::HIGHSHELF;
                }
            } else {
                filterType = dsp::TBiquadFilter<float_4>::PEAK;  // Fallback
            }

            // Configure stereo SIMD filters with mode support
            bands[band][0].setParameters(filterType, fc, Qeff, V);
            bands[band][1].setParameters(filterType, fc, Qeff, V);

            bandCache[band].f0 = f0;
            bandCache[band].Q = Qeff;
            bandCache[band].g = gain;
            bandCache[band].mode = mode;
            bandCache[band].sampleRate = sampleRate;  // Cache sample rate
        }
    }


    void process(const ProcessArgs &args) override {
        // Check bypass
        bool bypassed = params[BYPASS_PARAM].getValue() > 0.5f;

        // Update analog mode
        int analogModeInt = (int)std::round(params[ANALOG_MODE_PARAM].getValue());
        SafeAnalogProcessor::AnalogMode analogMode =
            (SafeAnalogProcessor::AnalogMode)rack::math::clamp(analogModeInt, 0, 3);

        analogProcessorL.setMode(analogMode);
        analogProcessorR.setMode(analogMode);

        // Mode values needed outside light divider for Cut mode logic
        float b1ModeValue = params[B1_MODE_PARAM].getValue();
        float b4ModeValue = params[B4_MODE_PARAM].getValue();

        // Update lights at reduced rate (every 256 samples = 187.5Hz at 48kHz)
        bool updateLights = lightDivider.process();
        if (updateLights) {
            // Bypass LED
            lights[BYPASS_LIGHT].setBrightness(bypassed ? 0.65f : 0.0f);

            // Analog mode LED (RGB)
            // Standard VCV Rack RedGreenBlueLight: [0]=Red, [1]=Green, [2]=Blue
            switch(analogMode) {
                case SafeAnalogProcessor::TRANSPARENT:
                    lights[ANALOG_LIGHT + 0].setBrightness(0.0f);  // Red
                    lights[ANALOG_LIGHT + 1].setBrightness(0.0f);  // Green
                    lights[ANALOG_LIGHT + 2].setBrightness(0.0f);  // Blue
                    break;
                case SafeAnalogProcessor::LIGHT:
                    lights[ANALOG_LIGHT + 0].setBrightness(0.0f);  // Red
                    lights[ANALOG_LIGHT + 1].setBrightness(0.5f);  // Green
                    lights[ANALOG_LIGHT + 2].setBrightness(0.0f);  // Blue
                    break;
                case SafeAnalogProcessor::MEDIUM:
                    lights[ANALOG_LIGHT + 0].setBrightness(0.0f);  // Red
                    lights[ANALOG_LIGHT + 1].setBrightness(0.0f);  // Green
                    lights[ANALOG_LIGHT + 2].setBrightness(0.5f);  // Blue
                    break;
                case SafeAnalogProcessor::FULL:
                    lights[ANALOG_LIGHT + 0].setBrightness(0.5f);  // Red
                    lights[ANALOG_LIGHT + 1].setBrightness(0.0f);  // Green
                    lights[ANALOG_LIGHT + 2].setBrightness(0.0f);  // Blue
                    break;
            }

            // Oversampling indicator light
            lights[OVERSAMPLE_LIGHT].setBrightness(params[OVERSAMPLE_PARAM].getValue() > 0.5f ? 1.0f : 0.0f);

            // LF mode lights (3 positions: high-pass, bell, shelf)
            lights[B1_MODE_LIGHT + 0].setBrightness(b1ModeValue == 2.0f ? 0.7f : 0.0f); // High-pass (top)
            lights[B1_MODE_LIGHT + 1].setBrightness(b1ModeValue == 1.0f ? 0.7f : 0.0f); // Bell (middle)
            lights[B1_MODE_LIGHT + 2].setBrightness(b1ModeValue == 0.0f ? 0.7f : 0.0f); // Shelf (bottom)

            // HF mode lights (3 positions: low-pass, bell, shelf)
            lights[B4_MODE_LIGHT + 0].setBrightness(b4ModeValue == 2.0f ? 0.7f : 0.0f); // Low-pass (top)
            lights[B4_MODE_LIGHT + 1].setBrightness(b4ModeValue == 1.0f ? 0.7f : 0.0f); // Bell (middle)
            lights[B4_MODE_LIGHT + 2].setBrightness(b4ModeValue == 0.0f ? 0.7f : 0.0f); // Shelf (bottom)

            // Clipping indicator (Shelves-inspired RGB display)
            double clipLevelL = analogProcessorL.getClippingLevel();
            double clipLevelR = analogProcessorR.getClippingLevel();
            double maxClipLevel = std::max(clipLevelL, clipLevelR);

            // RGB clipping indicator: green->amber->red progression with optimized brightness
            if (maxClipLevel < 0.1) {
                // No clipping - Pure GREEN at project standard brightness (PERFECT)
                lights[CLIP_LIGHT + 0].setBrightness(0.0f);           // Red OFF
                lights[CLIP_LIGHT + 1].setBrightness(0.5f);           // Green 0.5f
                lights[CLIP_LIGHT + 2].setBrightness(0.0f);           // Blue OFF
            } else {
                // Clipping detected - enhanced brightness progression
                float redIntensity = maxClipLevel * 0.9f;             // Scale red to 0.9f max (nearly full)
                // Green fades faster - completely OFF by maxClipLevel 0.6
                float greenIntensity = (maxClipLevel < 0.6f) ? 0.7f * (0.6f - maxClipLevel) / 0.6f : 0.0f;
                lights[CLIP_LIGHT + 0].setBrightness(redIntensity);   // Red variable to nearly full
                lights[CLIP_LIGHT + 1].setBrightness(greenIntensity); // Green fades to pure red
                lights[CLIP_LIGHT + 2].setBrightness(0.0f);          // Blue OFF
            }
        }

        // Event-based mode change detection for Cut mode gain locking
        // Check B1 (LF) mode changes
        if (b1ModeValue != lastB1Mode) {
            if (b1ModeValue >= 1.9f) {  // Switching TO Cut mode (value 2)
                b1GainLocked = true;
                params[B1_GAIN_PARAM].setValue(0.0f);  // Reset gain to center
                params[B1_FREQ_PARAM].setValue(std::log2(20.0f));  // Set to 20Hz (no effect)
            } else {  // Switching FROM Cut mode
                b1GainLocked = false;
            }
            lastB1Mode = b1ModeValue;
        }

        // Force gain to 0 while in Cut mode
        if (b1GainLocked) {
            params[B1_GAIN_PARAM].setValue(0.0f);
        }

        // Check B4 (HF) mode changes
        if (b4ModeValue != lastB4Mode) {
            if (b4ModeValue >= 1.9f) {  // Switching TO Cut mode (value 2)
                b4GainLocked = true;
                params[B4_GAIN_PARAM].setValue(0.0f);  // Reset gain to center
                params[B4_FREQ_PARAM].setValue(std::log2(20000.0f));  // Set to 20kHz (no effect)
            } else {  // Switching FROM Cut mode
                b4GainLocked = false;
            }
            lastB4Mode = b4ModeValue;
        }

        // Force gain to 0 while in Cut mode
        if (b4GainLocked) {
            params[B4_GAIN_PARAM].setValue(0.0f);
        }

        // Get stereo inputs
        float inputL = inputs[AUDIO_INPUT_L].getVoltage();
        float inputR = inputs[AUDIO_INPUT_R].isConnected() ?
                      inputs[AUDIO_INPUT_R].getVoltage() : inputL;  // Mono fallback

        float outputL = inputL;
        float outputR = inputR;

        if (!bypassed) {
            // Check if oversampling is enabled (moved up to calculate correct rate)
            bool oversamplingEnabled = params[OVERSAMPLE_PARAM].getValue() > 0.5f;

            // Update coefficients at reduced rate (every 16 samples for efficiency)
            double baseSampleRate = args.sampleRate;
            double effectiveSampleRate = oversamplingEnabled ? (baseSampleRate * oversampling_) : baseSampleRate;
            if (++coefficientDivider >= 16) {
                coefficientDivider = 0;
                for (int i = 0; i < 4; ++i) {
                    updateBandCoefficients(i, effectiveSampleRate);
                }
            }

            // Master gain with smoothing for click-free operation
            double masterGainDB = globalGainSmoother.process(params[GLOBAL_GAIN_PARAM].getValue());
            double masterGain = std::pow(10.0, masterGainDB / 20.0);

            if (oversamplingEnabled) {
                // Shelves true oversampling implementation (copied exact structure)
                float processedL = 0.0f, processedR = 0.0f;

                for (int i = 0; i < oversampling_; i++) {
                    // Zero-stuffing upsampling with anti-aliasing filters (Shelves pattern)
                    float upsampledL = up_filter_[0].Process((i == 0) ? (inputL * oversampling_) : 0.0f);
                    float upsampledR = up_filter_[1].Process((i == 0) ? (inputR * oversampling_) : 0.0f);

                    // Core processing chain at higher sample rate
                    float yL = analogProcessorL.process(upsampledL, vcaCompressionEnabled);
                    float yR = analogProcessorR.process(upsampledR, vcaCompressionEnabled);

                    // EQ processing
                    for (int band = 0; band < 4; ++band) {
                        float_4 sigL = float_4(yL, 0.0f, 0.0f, 0.0f);
                        float_4 sigR = float_4(yR, 0.0f, 0.0f, 0.0f);
                        sigL = bands[band][0].process(sigL);
                        sigR = bands[band][1].process(sigR);
                        yL = sigL[0];
                        yR = sigR[0];
                    }

                    // Clamping before downsampling (Shelves pattern)
                    yL = simd::clamp(yL, -10.5f, 10.5f);
                    yR = simd::clamp(yR, -10.5f, 10.5f);

                    // Downsampling with anti-aliasing filters (Shelves pattern)
                    processedL = down_filter_[0].Process(yL);
                    processedR = down_filter_[1].Process(yR);
                }

                outputL = processedL * masterGain;
                outputR = processedR * masterGain;

            } else {
                // Standard processing without oversampling
                double yL = inputL, yR = inputR;

                // Stage 1: Analog input processing
                yL = analogProcessorL.process(yL, vcaCompressionEnabled);
                yR = analogProcessorR.process(yR, vcaCompressionEnabled);

                // Stage 2: EQ processing chain
                for (int band = 0; band < 4; ++band) {
                    // Pack into SIMD for L channel
                    float_4 signalL = float_4(yL, 0.0f, 0.0f, 0.0f);
                    signalL = bands[band][0].process(signalL);
                    yL = signalL[0];

                    // Pack into SIMD for R channel
                    float_4 signalR = float_4(yR, 0.0f, 0.0f, 0.0f);
                    signalR = bands[band][1].process(signalR);
                    yR = signalR[0];
                }

                // Stage 3: Output gain
                outputL = yL * masterGain;
                outputR = yR * masterGain;
            }

            // Final clipping detection (post-EQ, post-master gain)
            analogProcessorL.updateClippingDetector(outputL);
            analogProcessorR.updateClippingDetector(outputR);

            // Final output clamping (VCV Rack compliance)
            outputL = simd::clamp(outputL, -10.5f, 10.5f);
            outputR = simd::clamp(outputR, -10.5f, 10.5f);
        }

        // Feed signals to spectrum analyzer with auto-shutdown after 8 seconds of inactivity
        // Thread-safe access: check shutdown flag before accessing spectrumAnalyzer
        if (!isShuttingDown.load() && spectrumAnalyzer) {
            bool analyserOn = params[ANALYSER_ENABLE_PARAM].getValue() > 0.5f;

            if (analyserOn) {
                // Display is ON - ensure DSP active and reset timer
                if (!analyzerDSPActive) {
                    spectrumAnalyzer->startWorkerThread();
                    analyzerDSPActive = true;
                }
                analyzerIdleTimer = 0.0f;
                spectrumAnalyzer->setSampleRate(args.sampleRate);
                spectrumAnalyzer->addSample(outputL, outputR);
            } else {
                // Display is OFF
                if (analyzerDSPActive) {
                    // Still active - feed silence for graceful fade
                    spectrumAnalyzer->setSampleRate(args.sampleRate);
                    spectrumAnalyzer->addSample(0.0f, 0.0f);

                    // Increment idle timer
                    analyzerIdleTimer += args.sampleTime;

                    // After 8 seconds, shut down worker thread
                    if (analyzerIdleTimer >= 8.0f) {
                        spectrumAnalyzer->stopWorkerThread();
                        analyzerDSPActive = false;
                    }
                }
                // else: DSP already off, do nothing (maximum efficiency)
            }
        }

        // Set outputs
        outputs[AUDIO_OUTPUT_L].setVoltage(outputL);
        outputs[AUDIO_OUTPUT_R].setVoltage(outputR);
    }

    json_t* dataToJson() override {
        json_t* root_j = json_object();
        json_object_set_new(root_j, "vcaCompressionEnabled", json_boolean(vcaCompressionEnabled));
        json_object_set_new(root_j, "enableProportionalQ", json_boolean(enableProportionalQ));
        return root_j;
    }

    void dataFromJson(json_t* root_j) override {
        json_t* vcaCompressionJ = json_object_get(root_j, "vcaCompressionEnabled");
        if (vcaCompressionJ)
            vcaCompressionEnabled = json_boolean_value(vcaCompressionJ);

        json_t* enableProportionalQJ = json_object_get(root_j, "enableProportionalQ");
        if (enableProportionalQJ)
            enableProportionalQ = json_boolean_value(enableProportionalQJ);
    }
};

// Custom LED that becomes physically invisible when brightness is 0.0f
struct InvisibleWhenOffLight : TinyLight<YellowLight> {
    void draw(const DrawArgs& args) override {
        if (module && firstLightId >= 0 && module->lights[firstLightId].getBrightness() <= 0.0f) {
            return; // Don't render anything when off - physically invisible
        }
        TinyLight<YellowLight>::draw(args);
    }
};


// Spectrum Display Widget for C1EQ display area
struct SpectrumDisplayWidget : LedDisplay {
    Module* module = nullptr;
    EqAnalysisEngine* engine = nullptr;

    void drawBackground(const DrawArgs& args) {
        // Inner box with grey thin line and minimal rounded corners
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 2, 2, box.size.x - 4, box.size.y - 4, 1.0f);
        nvgStrokeColor(args.vg, nvgRGBA(0x6b, 0x4a, 0x20, 255)); // Amber accent line
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
    }

    void drawSpectrum(const DrawArgs& args) {
        if (!module || !engine) return;

        const float* leftSpectrum = engine->getLeftSpectrum();
        const float* rightSpectrum = engine->getRightSpectrum();
        const float* leftPeakHold = engine->getLeftPeakHold();
        const float* rightPeakHold = engine->getRightPeakHold();

        // Define inner area bounds (matching the border)
        float innerX = 2.0f;
        float innerY = 2.0f;
        float innerWidth = box.size.x - 4.0f;
        float innerHeight = box.size.y - 4.0f;

        float barWidth = innerWidth / (float)EqAnalysisEngine::DISPLAY_BANDS;

        for (int i = 0; i < EqAnalysisEngine::DISPLAY_BANDS; i++) {
            float x = innerX + (i * barWidth);

            // Standard analyzer: uniform bar width, no overlap
            float actualBarWidth = barWidth;

            // Left channel spectrum
            float leftHeight = leftSpectrum[i] * (innerHeight - 2.0f) * 2.0f;
            leftHeight = clamp(leftHeight, 0.0f, innerHeight - 2.0f);
            if (leftHeight > 1.0f) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x, (innerY + innerHeight) - leftHeight, actualBarWidth, leftHeight);
                float leftFade = leftSpectrum[i] / std::max(leftPeakHold[i], 0.001f);
                leftFade = clamp(leftFade, 0.3f, 1.0f);
                NVGpaint leftGradient = nvgLinearGradient(args.vg, x, innerY + innerHeight, x, (innerY + innerHeight) - leftHeight,
                    nvgRGBA(255, 192, 80, (int)(255 * leftFade)),
                    nvgRGBA(127, 96, 40, (int)(128 * leftFade)));
                nvgFillPaint(args.vg, leftGradient);
                nvgFill(args.vg);
            }

            // Right channel spectrum
            float rightHeight = rightSpectrum[i] * (innerHeight - 2.0f) * 2.0f;
            rightHeight = clamp(rightHeight, 0.0f, innerHeight - 2.0f);
            if (rightHeight > 1.0f) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x, (innerY + innerHeight) - rightHeight, actualBarWidth, rightHeight);
                float rightFade = rightSpectrum[i] / std::max(rightPeakHold[i], 0.001f);
                rightFade = clamp(rightFade, 0.3f, 1.0f);
                NVGpaint rightGradient = nvgLinearGradient(args.vg, x, innerY + innerHeight, x, (innerY + innerHeight) - rightHeight,
                    nvgRGBA(235, 170, 50, (int)(128 * rightFade)),
                    nvgRGBA(117, 85, 25, (int)(64 * rightFade)));
                nvgFillPaint(args.vg, rightGradient);
                nvgFill(args.vg);
            }

            // Single unified peak hold indicator (maximum of both channels)
            float leftPeakHeight = leftPeakHold[i] * (innerHeight - 2.0f) * 2.0f;
            float rightPeakHeight = rightPeakHold[i] * (innerHeight - 2.0f) * 2.0f;
            float unifiedPeakHeight = std::max(leftPeakHeight, rightPeakHeight);
            unifiedPeakHeight = clamp(unifiedPeakHeight, 0.0f, innerHeight - 2.0f);

            if (unifiedPeakHeight > 1.0f) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x, (innerY + innerHeight) - unifiedPeakHeight, actualBarWidth, 1.0f);
                nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 180));  // Single clean peak line
                nvgFill(args.vg);
            }
        }
    }

    void draw(const DrawArgs& args) override {
        drawBackground(args);
        drawSpectrum(args);
    }
};

// C1 Gain Knob with Cut Mode Lock - disables mouse input when in Cut mode
struct C1GainKnobWithCutLock : C1Knob280 {
    bool isB1Gain = false;  // true for B1_GAIN, false for B4_GAIN

    // Safe accessor - checks module pointer validity before accessing flag
    bool isLocked() {
        if (!module) return false;  // Module destroyed, always unlocked
        C1EQ* eqModule = static_cast<C1EQ*>(module);
        return isB1Gain ? eqModule->b1GainLocked : eqModule->b4GainLocked;
    }

    void onButton(const ButtonEvent& e) override {
        if (isLocked()) {
            e.consume(this);  // Block mouse button in Cut mode
            return;
        }
        C1Knob280::onButton(e);
    }

    void onDragStart(const DragStartEvent& e) override {
        if (isLocked()) {
            e.consume(this);  // Block drag start in Cut mode
            return;
        }
        C1Knob280::onDragStart(e);
    }

    void onDragMove(const DragMoveEvent& e) override {
        if (isLocked()) {
            return;  // Block drag move in Cut mode
        }
        C1Knob280::onDragMove(e);
    }

    void onDragEnd(const DragEndEvent& e) override {
        if (isLocked()) {
            return;  // Block drag end in Cut mode
        }
        C1Knob280::onDragEnd(e);
    }

    void onDoubleClick(const DoubleClickEvent& e) override {
        if (isLocked()) {
            e.consume(this);  // Block double-click in Cut mode
            return;
        }
        C1Knob280::onDoubleClick(e);
    }
};

// C1EQ Widget with TC design (24HP)
struct C1EQWidget : ModuleWidget {
    C1EQWidget(C1EQ* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/C1EQ.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 4 Parametric bands in 4 vertical columns (LF, LMF, HMF, HF)
        // Using pixel coordinates for 24HP (360px) panel

        // TC theme custom button with dynamic ivory/dark gray background
        struct OrangeLight : GrayModuleLightWidget {
            OrangeLight() {
                addBaseColor(SCHEME_ORANGE);
                // Add darker border for button definition on ivory background
                borderColor = nvgRGBA(0x60, 0x60, 0x60, 0xFF);
            }

            void drawBackground(const DrawArgs& args) override {
                // Dynamic background: dull ivory when off, dark gray when lit
                bool isLit = false;
                if (module && firstLightId >= 0) {
                    isLit = (module->lights[firstLightId].getBrightness() > 0.01f);
                }

                if (isLit) {
                    // Light is lit - use dark gray background to make amber pop
                    bgColor = nvgRGB(0x0c, 0x0c, 0x0c);
                } else {
                    // Light is off - use dull toned-down ivory background
                    bgColor = nvgRGB(0xB8, 0xB4, 0xAC);
                }

                // Call parent drawBackground method
                LightWidget::drawBackground(args);
            }

            void drawHalo(const DrawArgs& args) override {
                // Don't draw halo if rendering in framebuffer
                if (args.fb)
                    return;

                const float halo = settings::haloBrightness;
                if (halo == 0.f)
                    return;

                // If light is off, rendering the halo gives no effect
                if (color.r == 0.f && color.g == 0.f && color.b == 0.f)
                    return;

                math::Vec c = box.size.div(2);
                float radius = std::min(box.size.x, box.size.y) / 2.0;
                // Reduced halo: use 2x radius instead of 4x, max 8 instead of 15
                float oradius = radius + std::min(radius * 2.f, 8.f);

                nvgBeginPath(args.vg);
                nvgRect(args.vg, c.x - oradius, c.y - oradius, 2 * oradius, 2 * oradius);

                NVGcolor icol = color::mult(color, halo);
                NVGcolor ocol = nvgRGBA(0, 0, 0, 0);
                NVGpaint paint = nvgRadialGradient(args.vg, c.x, c.y, radius, oradius, icol, ocol);
                nvgFillPaint(args.vg, paint);
                nvgFill(args.vg);
            }
        };

        struct C1WhiteRoundButton : app::SvgSwitch {
            app::ModuleLightWidget* light;

            C1WhiteRoundButton() {
                momentary = false;
                latch = true;

                // Add custom 75% scaled SVG frames
                addFrame(Svg::load(asset::plugin(pluginInstance, "res/CustomButton_0.svg")));
                addFrame(Svg::load(asset::plugin(pluginInstance, "res/CustomButton_1.svg")));

                // Create and add amber light widget - scaled proportionally to 75%
                light = new MediumSimpleLight<OrangeLight>;
                light->box.size = light->box.size.mult(0.75f);
                // Center light on button
                light->box.pos = box.size.div(2).minus(light->box.size.div(2));
                addChild(light);
            }

            app::ModuleLightWidget* getLight() {
                return light;
            }
        };

        // Bypass button - positioned to match Shape module's left edge clearance and Y position
        C1WhiteRoundButton* bypassButton = createParamCentered<C1WhiteRoundButton>(Vec(23, 26), module, C1EQ::BYPASS_PARAM);
        bypassButton->getLight()->module = module;
        if (module) {
            bypassButton->getLight()->firstLightId = C1EQ::BYPASS_LIGHT;
        }
        addParam(bypassButton);

        // LF Band (Column 1) - X=35px with mode toggle (no Q knob - hardcoded Q=0.8) - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(35, 175), module, C1EQ::B1_FREQ_PARAM));
        LedRingOverlay* b1FreqRing = new LedRingOverlay(module, C1EQ::B1_FREQ_PARAM);
        b1FreqRing->box.pos = Vec(35 - 25, 175 - 25);
        addChild(b1FreqRing);

        C1GainKnobWithCutLock* b1GainKnob = createParamCentered<C1GainKnobWithCutLock>(Vec(35, 225), module, C1EQ::B1_GAIN_PARAM);
        b1GainKnob->isB1Gain = true;  // Identify as B1 gain knob
        addParam(b1GainKnob);
        LedRingOverlay* b1GainRing = new LedRingOverlay(module, C1EQ::B1_GAIN_PARAM);
        b1GainRing->box.pos = Vec(35 - 25, 225 - 25);
        addChild(b1GainRing);

        // LF mode button (cycles 0→1→2→0: Shelf→Bell→Cut)
        C1WhiteRoundButton* b1ModeButton = createParamCentered<C1WhiteRoundButton>(Vec(23, 131), module, C1EQ::B1_MODE_PARAM);
        b1ModeButton->getLight()->module = module;
        if (module) {
            b1ModeButton->getLight()->firstLightId = C1EQ::B1_MODE_BUTTON_LIGHT;
        }
        addParam(b1ModeButton);

        // LMF Band (Column 2) - X=85px - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(85, 175), module, C1EQ::B2_FREQ_PARAM));
        LedRingOverlay* b2FreqRing = new LedRingOverlay(module, C1EQ::B2_FREQ_PARAM);
        b2FreqRing->box.pos = Vec(85 - 25, 175 - 25);
        addChild(b2FreqRing);

        addParam(createParamCentered<C1Knob280>(Vec(85, 125), module, C1EQ::B2_Q_PARAM));
        LedRingOverlay* b2QRing = new LedRingOverlay(module, C1EQ::B2_Q_PARAM);
        b2QRing->box.pos = Vec(85 - 25, 125 - 25);
        addChild(b2QRing);

        addParam(createParamCentered<C1Knob280>(Vec(85, 225), module, C1EQ::B2_GAIN_PARAM));
        LedRingOverlay* b2GainRing = new LedRingOverlay(module, C1EQ::B2_GAIN_PARAM);
        b2GainRing->box.pos = Vec(85 - 25, 225 - 25);
        addChild(b2GainRing);

        // HMF Band (Column 3) - X=135px - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(135, 175), module, C1EQ::B3_FREQ_PARAM));
        LedRingOverlay* b3FreqRing = new LedRingOverlay(module, C1EQ::B3_FREQ_PARAM);
        b3FreqRing->box.pos = Vec(135 - 25, 175 - 25);
        addChild(b3FreqRing);

        addParam(createParamCentered<C1Knob280>(Vec(135, 125), module, C1EQ::B3_Q_PARAM));
        LedRingOverlay* b3QRing = new LedRingOverlay(module, C1EQ::B3_Q_PARAM);
        b3QRing->box.pos = Vec(135 - 25, 125 - 25);
        addChild(b3QRing);

        addParam(createParamCentered<C1Knob280>(Vec(135, 225), module, C1EQ::B3_GAIN_PARAM));
        LedRingOverlay* b3GainRing = new LedRingOverlay(module, C1EQ::B3_GAIN_PARAM);
        b3GainRing->box.pos = Vec(135 - 25, 225 - 25);
        addChild(b3GainRing);

        // HF Band (Column 4) - X=185px with mode toggle (no Q knob - hardcoded Q=1.0) - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(185, 175), module, C1EQ::B4_FREQ_PARAM));
        LedRingOverlay* b4FreqRing = new LedRingOverlay(module, C1EQ::B4_FREQ_PARAM);
        b4FreqRing->box.pos = Vec(185 - 25, 175 - 25);
        addChild(b4FreqRing);

        C1GainKnobWithCutLock* b4GainKnob = createParamCentered<C1GainKnobWithCutLock>(Vec(185, 225), module, C1EQ::B4_GAIN_PARAM);
        b4GainKnob->isB1Gain = false;  // Identify as B4 gain knob
        addParam(b4GainKnob);
        LedRingOverlay* b4GainRing = new LedRingOverlay(module, C1EQ::B4_GAIN_PARAM);
        b4GainRing->box.pos = Vec(185 - 25, 225 - 25);
        addChild(b4GainRing);

        // HF mode button (cycles 0→1→2→0: Shelf→Bell→Cut)
        C1WhiteRoundButton* b4ModeButton = createParamCentered<C1WhiteRoundButton>(Vec(173, 131), module, C1EQ::B4_MODE_PARAM);
        b4ModeButton->getLight()->module = module;
        if (module) {
            b4ModeButton->getLight()->firstLightId = C1EQ::B4_MODE_BUTTON_LIGHT;
        }
        addParam(b4ModeButton);

        // Master controls - aligned with bottom jacks Y center - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(85, 309), module, C1EQ::GLOBAL_GAIN_PARAM));
        LedRingOverlay* globalGainRing = new LedRingOverlay(module, C1EQ::GLOBAL_GAIN_PARAM);
        globalGainRing->box.pos = Vec(85 - 25, 309 - 25);
        addChild(globalGainRing);

        addParam(createParamCentered<C1SnapKnob280>(Vec(135, 309), module, C1EQ::ANALOG_MODE_PARAM));
        LedRingOverlaySkip4* analogModeRing = new LedRingOverlaySkip4(module, C1EQ::ANALOG_MODE_PARAM);
        analogModeRing->box.pos = Vec(135 - 25, 309 - 25);
        addChild(analogModeRing);

        // Oversample switch moved 50px up
        addParam(createParamCentered<CKSS>(Vec(110, 279), module, C1EQ::OVERSAMPLE_PARAM));


        addInput(createInputCentered<ThemedPJ301MPort>(Vec(35, 284), module, C1EQ::AUDIO_INPUT_L));
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(35, 314), module, C1EQ::AUDIO_INPUT_R));

        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(185, 284), module, C1EQ::AUDIO_OUTPUT_L));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(185, 314), module, C1EQ::AUDIO_OUTPUT_R));

        // Status lights positioned above their respective encoders
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(75, 284), module, C1EQ::CLIP_LIGHT));      // Above GAIN encoder (10px left)
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(145, 284), module, C1EQ::ANALOG_LIGHT));   // Above MODEL encoder (10px right)

        // Oversample indicator light (physically invisible when off)
        addChild(createLightCentered<InvisibleWhenOffLight>(Vec(110, 284), module, C1EQ::OVERSAMPLE_LIGHT));

        // LF mode lights (3 positions: high-pass, bell, shelf) - right of LF switch
        addChild(createLightCentered<TinyLight<YellowLight>>(Vec(35, 122), module, C1EQ::B1_MODE_LIGHT + 0)); // Top - High-pass
        addChild(createLightCentered<TinyLight<YellowLight>>(Vec(35, 131), module, C1EQ::B1_MODE_LIGHT + 1)); // Middle - Bell
        addChild(createLightCentered<TinyLight<YellowLight>>(Vec(35, 140), module, C1EQ::B1_MODE_LIGHT + 2)); // Bottom - Shelf

        // HF mode lights (3 positions: low-pass, bell, shelf) - right of HF switch
        addChild(createLightCentered<TinyLight<YellowLight>>(Vec(185, 122), module, C1EQ::B4_MODE_LIGHT + 0)); // Top - Low-pass
        addChild(createLightCentered<TinyLight<YellowLight>>(Vec(185, 131), module, C1EQ::B4_MODE_LIGHT + 1)); // Middle - Bell
        addChild(createLightCentered<TinyLight<YellowLight>>(Vec(185, 140), module, C1EQ::B4_MODE_LIGHT + 2)); // Bottom - Shelf

        // Spectrum Display Widget - positioned in display area
        if (module) {
            SpectrumDisplayWidget* spectrumDisplay = createWidget<SpectrumDisplayWidget>(Vec(12, 41));
            spectrumDisplay->box.size = Vec(201, 54);
            spectrumDisplay->module = module;
            spectrumDisplay->engine = module->spectrumAnalyzer;
            module->spectrumAnalyzer = new EqAnalysisEngine();
            spectrumDisplay->engine = module->spectrumAnalyzer;
            addChild(spectrumDisplay);

            // Add screen toggle switch in upper right corner (same style as ChanIn/Shape)
            struct SimpleSwitch : widget::OpaqueWidget {
                C1EQ* module = nullptr;
                bool isHovered = false;
                float currentOpacity = 0.5f;
                double lastTime = 0.0;

                void draw(const DrawArgs& args) override {
                    float x = 2.0f;
                    float y = 2.0f;
                    float size = 5.6f;

                    // Smooth opacity transition
                    float targetOpacity = isHovered ? 1.0f : 0.5f;
                    double currentTime = glfwGetTime();
                    if (lastTime == 0.0) lastTime = currentTime;
                    float deltaTime = (float)(currentTime - lastTime);
                    lastTime = currentTime;

                    // Transition speed: ~5 units per second (200ms transition time)
                    float transitionSpeed = 5.0f;
                    if (currentOpacity < targetOpacity) {
                        currentOpacity = std::min(targetOpacity, currentOpacity + transitionSpeed * deltaTime);
                    } else if (currentOpacity > targetOpacity) {
                        currentOpacity = std::max(targetOpacity, currentOpacity - transitionSpeed * deltaTime);
                    }

                    float opacity = currentOpacity;

                    nvgBeginPath(args.vg);
                    nvgRoundedRect(args.vg, x, y, size, size, 1.0f);

                    // Get analyser enable state from module parameter
                    bool analyserOn = module ? (module->params[C1EQ::ANALYSER_ENABLE_PARAM].getValue() > 0.5f) : true;

                    // Fill with amber when analyser is enabled
                    if (analyserOn) {
                        nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, (int)(255 * opacity)));
                        nvgFill(args.vg);
                    }

                    nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, (int)(255 * opacity)));
                    nvgStrokeWidth(args.vg, 0.5f);
                    nvgStroke(args.vg);

                    // Draw cross (X) when analyser is disabled
                    if (!analyserOn) {
                        nvgStrokeColor(args.vg, nvgRGBA(200, 200, 200, (int)(255 * opacity)));
                        nvgStrokeWidth(args.vg, 0.8f);

                        // Draw X - two diagonal lines
                        float margin = 1.5f;
                        nvgBeginPath(args.vg);
                        nvgMoveTo(args.vg, x + margin, y + margin);
                        nvgLineTo(args.vg, x + size - margin, y + size - margin);
                        nvgMoveTo(args.vg, x + size - margin, y + margin);
                        nvgLineTo(args.vg, x + margin, y + size - margin);
                        nvgStroke(args.vg);
                    }
                }

                void onEnter(const EnterEvent& e) override {
                    isHovered = true;
                    OpaqueWidget::onEnter(e);
                }

                void onLeave(const LeaveEvent& e) override {
                    isHovered = false;
                    OpaqueWidget::onLeave(e);
                }

                void onButton(const ButtonEvent& e) override {
                    if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
                        // Toggle the analyser enable parameter
                        float currentValue = module->params[C1EQ::ANALYSER_ENABLE_PARAM].getValue();
                        module->params[C1EQ::ANALYSER_ENABLE_PARAM].setValue(currentValue > 0.5f ? 0.0f : 1.0f);
                        e.consume(this);
                    }
                }
            };

            SimpleSwitch* simpleSwitch = new SimpleSwitch();
            simpleSwitch->module = module;
            simpleSwitch->box.pos = Vec(201, 43);  // Upper right corner, nudged left 4px
            simpleSwitch->box.size = Vec(12, 12);
            addChild(simpleSwitch);
        }

        // NanoVG text labels (TC house style)
        addTextLabels();
    }

    void addTextLabels() {
        // I/O Labels (matching Shape module style)
        struct InLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "IN", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "IN", NULL);
            }
        };

        InLabel* inLabel = new InLabel();
        inLabel->box.pos = Vec(35, 335);
        inLabel->box.size = Vec(20, 10);         addChild(inLabel);

        struct OutLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "OUT", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "OUT", NULL);
            }
        };

        OutLabel* outLabel = new OutLabel();
        outLabel->box.pos = Vec(185, 335);
        outLabel->box.size = Vec(20, 10);         addChild(outLabel);

        // Main title
        struct TitleLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 18.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                // Black outline
                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "EQUALIZER", NULL);
                        }
                    }
                }

                // White text
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "EQUALIZER", NULL);
            }
        };
        TitleLabel* titleLabel = new TitleLabel;
        titleLabel->box.pos = Vec(112.5, 10);
        titleLabel->box.size = Vec(100, 20);
        addChild(titleLabel);


        // Band labels (centered above each column)
        addBandLabel("LF", 35, 103);
        addBandLabel("LMF", 85, 103);
        addBandLabel("HMF", 135, 103);
        addBandLabel("HF", 185, 103);

        // LF Band (Column 1) parameter labels - positioned right of controls (FREQ moved to Y=140)
        addParamLabel("FREQ", 35, 200);  // Centered under LF FREQ knob, nudged 2px left
        addParamLabel("GAIN", 35, 250);  // Centered under LF GAIN knob (35), nudged 2px left

        // LMF Band (Column 2) parameter labels - positioned right of controls (positions swapped)
        addParamLabel("FREQ", 85, 200); // Centered under LMF FREQ knob, nudged 2px left
        addParamLabel("Q", 85, 149);    // Centered under LMF Q knob, nudged right 5px, down 8px
        addParamLabel("GAIN", 85, 250); // Centered under LMF GAIN knob (85), nudged 2px left

        // HMF Band (Column 3) parameter labels - positioned right of controls (positions swapped)
        addParamLabel("FREQ", 135, 200); // Centered under HMF FREQ knob, nudged 2px left
        addParamLabel("Q", 135, 149);    // Centered under HMF Q knob, nudged right 5px, down 8px
        addParamLabel("GAIN", 135, 250); // Centered under HMF GAIN knob (135), nudged 2px left

        // HF Band (Column 4) parameter labels - positioned right of controls (FREQ moved to Y=140)
        addParamLabel("FREQ", 185, 200); // Centered under HF FREQ knob, nudged 2px left
        addParamLabel("GAIN", 185, 250); // Centered under HF GAIN knob (185), nudged 2px left


        // Master control labels - aligned with jack labels Y position
        addParamLabel("GAIN", 85, 330); // Moved 14px right from original position
        addParamLabel("MODEL", 135.5, 330); // Moved 12.5px right from original position

        // OS label inside oversample switch (upper half) - 7.0f font size
        // Visible when oversample is OFF, invisible when ON
        struct OsLabel : Widget {
            C1EQ* module;
            OsLabel(C1EQ* m) : module(m) {}

            void draw(const DrawArgs& args) override {
                if (!module) return;

                // Only draw when oversample is OFF (parameter value <= 0.5)
                if (module->params[C1EQ::OVERSAMPLE_PARAM].getValue() > 0.5f) {
                    return; // Don't draw when oversample is ON
                }

                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 7.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                // Black outline
                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "OS", NULL);
                        }
                    }
                }

                // White text
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "OS", NULL);
            }
        };
        OsLabel* osLabel = new OsLabel(static_cast<C1EQ*>(module));
        osLabel->box.pos = Vec(110, 274.5);
        osLabel->box.size = Vec(20, 10);
        addChild(osLabel);

        // Mode indicator labels for LF switch (right of LED stack)
        addParamLabel("C", 43, 122); // Cut/high-pass (top LED)
        addParamLabel("B", 43, 131); // Bell (middle LED)
        addParamLabel("S", 43, 140); // Shelf (bottom LED)

        // Mode indicator labels for HF switch (right of LED stack)
        addParamLabel("C", 193, 122); // Cut/low-pass (top LED)
        addParamLabel("B", 193, 131); // Bell (middle LED)
        addParamLabel("S", 193, 140); // Shelf (bottom LED)

        // Twisted Cable logo - using shared widget
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::FULL, module);
        tcLogo->box.pos = Vec(107, 355);
        addChild(tcLogo);
    }

    void addBandLabel(const char* text, float x, float y) {
        struct BandLabel : Widget {
            std::string labelText;
            BandLabel(std::string t) : labelText(t) {}
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 12.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                // Black outline
                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, labelText.c_str(), NULL);
                        }
                    }
                }

                // White text
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, labelText.c_str(), NULL);
            }
        };
        BandLabel* bandLabel = new BandLabel(text);
        bandLabel->box.pos = Vec(x, y);
        bandLabel->box.size = Vec(30, 14);
        addChild(bandLabel);
    }

    void addParamLabel(const char* text, float x, float y) {
        struct ParamLabel : Widget {
            std::string labelText;
            ParamLabel(std::string t) : labelText(t) {}
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                // Black outline
                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, labelText.c_str(), NULL);
                        }
                    }
                }

                // White text (match other modules)
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, labelText.c_str(), NULL);
            }
        };
        ParamLabel* paramLabel = new ParamLabel(text);
        paramLabel->box.pos = Vec(x, y);
        paramLabel->box.size = Vec(40, 8);
        addChild(paramLabel);
    }

    void addVerticalLabel(const char* text, float x, float y) {
        struct VerticalLabel : Widget {
            std::string labelText;
            VerticalLabel(std::string t) : labelText(t) {}
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 7.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                float letterSpacing = 5.0f;
                int len = labelText.length();

                for (int i = 0; i < len; i++) {
                    char letter[2] = {labelText[i], '\0'};
                    float yOffset = i * letterSpacing;

                    // Black outline
                    nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
                    for (int dx = -1; dx <= 1; dx++) {
                        for (int dy = -1; dy <= 1; dy++) {
                            if (dx != 0 || dy != 0) {
                                nvgText(args.vg, dx * 0.5f, yOffset + dy * 0.5f, letter, NULL);
                            }
                        }
                    }

                    // White text
                    nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                    nvgText(args.vg, 0, yOffset, letter, NULL);
                }
            }
        };
        VerticalLabel* verticalLabel = new VerticalLabel(text);
        verticalLabel->box.pos = Vec(x, y);
        verticalLabel->box.size = Vec(20, 60);
        addChild(verticalLabel);
    }

    void appendContextMenu(Menu* menu) override {
        C1EQ* module = dynamic_cast<C1EQ*>(this->module);

        menu->addChild(new MenuSeparator);

        menu->addChild(createBoolPtrMenuItem("Enable VCA Compression", "", &module->vcaCompressionEnabled));
        menu->addChild(createBoolPtrMenuItem("Enable Proportional Q", "", &module->enableProportionalQ));
    }
};

} // namespace

// Model registration with proper VCV Rack integration
Model* modelC1EQ = createModel<C1EQ, C1EQWidget>("C1EQ");