#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include "../shared/include/CrossPluginInterface.h"
#include <dsp/digital.hpp>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>

// Custom ParamQuantity for Phase Invert button with OFF/INVERTED labels
struct PhaseParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "INVERTED";
    }
};

namespace {

// Forward declarations
struct MeteringSwitchWidget;

// TC Custom C1Knob - Graphite/Anthracite encoder with amber indicator
struct C1Knob : RoundKnob {
    C1Knob() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
        bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
    }
};

// C1 Knob with 280° rotation range
struct C1Knob280 : RoundKnob {
    C1Knob280() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
        bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/C1Knob_bg.svg")));
        minAngle = -140.0f * (M_PI / 180.0f);  // -2.443 radians
        maxAngle = +140.0f * (M_PI / 180.0f);  // +2.443 radians
    }
};

// LED Ring Overlay Widget
struct LedRingOverlay : widget::TransparentWidget {
    Module* module;
    int paramId;

    LedRingOverlay(Module* m, int pid) : module(m), paramId(pid) {
        box.size = Vec(50, 50);
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        ParamQuantity* pq = module->paramQuantities[paramId];
        if (!pq) return;

        // Get normalized parameter value (0-1) - handles bipolar parameters correctly
        float paramValue = pq->getScaledValue();

        // LED ring parameters
        const int dotCount = 15;
        const float gapDeg = 80.0f;
        const float gap = gapDeg * (M_PI / 180.0f);
        const float start = -M_PI * 1.5f + gap * 0.5f;  // -230°
        const float end   =  M_PI * 0.5f  - gap * 0.5f; // +50°
        const float totalSpan = end - start;  // 280° in radians

        // Ring geometry (matching ChanOut - adjusted for 85% scaled knob)
        const float knobRadius = 24.095f / 2.0f;         // 12.0475px knob radius
        const float ringOffset = 3.5f;                   // Distance from knob edge
        const float radius = knobRadius + ringOffset;    // ~15.5px LED ring radius
        const float dotRadius = 0.9f;                    // LED dot radius
        const Vec center = Vec(box.size.x / 2.0f, box.size.y / 2.0f);

        // Calculate exact fractional position for smooth LED fading
        float exactPos = paramValue * (dotCount - 1);  // 0.0 to 14.0
        int led1 = (int)exactPos;                      // LED before (floor)
        int led2 = led1 + 1;                           // LED after
        float frac = exactPos - led1;                  // 0.0 to 1.0 between LEDs

        // Clamp LED indices
        led1 = clamp(led1, 0, dotCount - 1);
        led2 = clamp(led2, 0, dotCount - 1);

        // Draw 15 LEDs around the arc with smooth crossfade
        for (int i = 0; i < dotCount; ++i) {
            float t = (float)i / (dotCount - 1);
            float angle = start + t * totalSpan;

            float x = center.x + radius * std::cos(angle);
            float y = center.y + radius * std::sin(angle);

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
            nvgCircle(args.vg, x, y, dotRadius);
            nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, alpha));
            nvgFill(args.vg);
        }
    }
};

// Forward declaration
struct ChanIn;

// Expander message struct for CHAN-IN-CV communication
struct ChanInExpanderMessage {
    float levelCV;        // -1.0 to +1.0 (attenuated, represents ±12dB range)
    float hpfFreqCV;      // -5V to +5V (1V/oct for frequency control)
    float lpfFreqCV;      // -5V to +5V (1V/oct for frequency control)
    float phaseInvertCV;  // Gate: >1V = invert phase
};

// RMS Metering Display - Sleek horizontal bar meter
struct RMSMeterDisplay : widget::TransparentWidget {
    Module* module;

    // RMS calculation parameters
    static constexpr int RMS_WINDOW_SIZE = 2048;  // ~43ms at 48kHz for smooth RMS
    static constexpr float RMS_ALPHA = 0.05f;     // Smoothing factor for RMS integration

    // Visual parameters
    float display_width = 88.0f;    // 4px clearance from each side (96-8=88)
    float display_height = 7.5f;    // Share 30px remaining height with 4 meters (30/4=7.5)

    // RMS state
    float rms_left = 0.0f;
    float rms_right = 0.0f;
    float smoothed_rms_left = 0.0f;
    float smoothed_rms_right = 0.0f;

    // Peak hold state
    float peak_hold_left = 0.0f;
    float peak_hold_right = 0.0f;
    float peak_hold_timer_left = 0.0f;
    float peak_hold_timer_right = 0.0f;
    static constexpr float PEAK_HOLD_DECAY_TIME = 0.5f;  // 0.5 seconds

    // Running RMS calculation
    float sum_squares_left = 0.0f;
    float sum_squares_right = 0.0f;
    int sample_count = 0;
    int rms_decimation_counter = 0;

    // TC color scheme - Amber meter color
    uint8_t meter_r = 0xFF, meter_g = 0xC0, meter_b = 0x50;  // Amber (matching LED rings)
    uint8_t bg_r = 40, bg_g = 40, bg_b = 40;                  // Dark background

    RMSMeterDisplay(Module* module) : module(module) {}

    void reset() {
        // Reset all RMS state
        rms_left = 0.0f;
        rms_right = 0.0f;
        smoothed_rms_left = 0.0f;
        smoothed_rms_right = 0.0f;
        peak_hold_left = 0.0f;
        peak_hold_right = 0.0f;
        peak_hold_timer_left = 0.0f;
        peak_hold_timer_right = 0.0f;
        sum_squares_left = 0.0f;
        sum_squares_right = 0.0f;
        sample_count = 0;
        rms_decimation_counter = 0;
    }

    void addStereoSample(float left, float right) {
        if (!module) return;

        // Accumulate squares for RMS calculation
        sum_squares_left += left * left;
        sum_squares_right += right * right;
        sample_count++;

        // Calculate RMS every RMS_WINDOW_SIZE samples
        if (sample_count >= RMS_WINDOW_SIZE) {
            // Calculate RMS: sqrt(mean(squares))
            rms_left = std::sqrt(sum_squares_left / sample_count);
            rms_right = std::sqrt(sum_squares_right / sample_count);

            // Smooth the RMS values for display
            smoothed_rms_left += RMS_ALPHA * (rms_left - smoothed_rms_left);
            smoothed_rms_right += RMS_ALPHA * (rms_right - smoothed_rms_right);

            // Update peak hold
            if (smoothed_rms_left > peak_hold_left) {
                peak_hold_left = smoothed_rms_left;
                peak_hold_timer_left = PEAK_HOLD_DECAY_TIME;
            }
            if (smoothed_rms_right > peak_hold_right) {
                peak_hold_right = smoothed_rms_right;
                peak_hold_timer_right = PEAK_HOLD_DECAY_TIME;
            }

            // Decay peak hold timers
            peak_hold_timer_left = std::max(0.0f, peak_hold_timer_left - (RMS_WINDOW_SIZE / 48000.0f));
            peak_hold_timer_right = std::max(0.0f, peak_hold_timer_right - (RMS_WINDOW_SIZE / 48000.0f));

            // Decay peak hold to current value when timer expires
            if (peak_hold_timer_left <= 0.0f) {
                peak_hold_left = std::max(peak_hold_left - (RMS_WINDOW_SIZE / 48000.0f) * 10.0f, smoothed_rms_left);
            }
            if (peak_hold_timer_right <= 0.0f) {
                peak_hold_right = std::max(peak_hold_right - (RMS_WINDOW_SIZE / 48000.0f) * 10.0f, smoothed_rms_right);
            }

            // Reset accumulators
            sum_squares_left = 0.0f;
            sum_squares_right = 0.0f;
            sample_count = 0;
        }
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Background bar
        nvgFillColor(args.vg, nvgRGBA(bg_r, bg_g, bg_b, 255));
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, display_width, display_height, 2.0f);
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 80, 255));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        float left_db = (smoothed_rms_left > 0.0001f) ?
            20.0f * std::log10(smoothed_rms_left / 5.0f) : -60.0f;
        float right_db = (smoothed_rms_right > 0.0001f) ?
            20.0f * std::log10(smoothed_rms_right / 5.0f) : -60.0f;

        // Clamp to meter range (-60dB to +6dB)
        left_db = rack::math::clamp(left_db, -60.0f, 6.0f);
        right_db = rack::math::clamp(right_db, -60.0f, 6.0f);

        // Normalize to 0-1 range for bar width
        float left_norm = (left_db + 60.0f) / 66.0f;
        float right_norm = (right_db + 60.0f) / 66.0f;

        // Draw left channel bar (top half)
        float bar_height = (display_height - 1.0f) * 0.5f;
        float left_width = (display_width - 2.0f) * left_norm;

        if (left_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint leftGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                    // Start point (left edge)
                1.0f + left_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, leftGradient);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f, left_width, bar_height);
            nvgFill(args.vg);
        }

        // Draw right channel bar (bottom half)
        float right_width = (display_width - 2.0f) * right_norm;

        if (right_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint rightGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                     // Start point (left edge)
                1.0f + right_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, rightGradient);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f + bar_height, right_width, bar_height);
            nvgFill(args.vg);
        }

        // Peak hold indicators (white lines)
        float peak_hold_left_db = (peak_hold_left > 0.0001f) ?
            20.0f * std::log10(peak_hold_left / 5.0f) : -60.0f;
        float peak_hold_right_db = (peak_hold_right > 0.0001f) ?
            20.0f * std::log10(peak_hold_right / 5.0f) : -60.0f;

        peak_hold_left_db = rack::math::clamp(peak_hold_left_db, -60.0f, 6.0f);
        peak_hold_right_db = rack::math::clamp(peak_hold_right_db, -60.0f, 6.0f);

        float peak_hold_left_norm = (peak_hold_left_db + 60.0f) / 66.0f;
        float peak_hold_right_norm = (peak_hold_right_db + 60.0f) / 66.0f;

        if (peak_hold_left > 0.0001f) {
            float peak_x = 1.0f + (display_width - 2.0f) * peak_hold_left_norm;
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, peak_x, 0.5f);
            nvgLineTo(args.vg, peak_x, 0.5f + bar_height);
            nvgStroke(args.vg);
        }

        if (peak_hold_right > 0.0001f) {
            float peak_x = 1.0f + (display_width - 2.0f) * peak_hold_right_norm;
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, peak_x, 0.5f + bar_height);
            nvgLineTo(args.vg, peak_x, display_height - 0.5f);
            nvgStroke(args.vg);
        }

        // 0dB reference line (thin grey line behind bars)
        // 0dB is at 60dB into the 66dB range (-60 to +6), so normalized position is 60/66
        float zero_db_norm = 60.0f / 66.0f;
        float zero_db_x = 1.0f + (display_width - 2.0f) * zero_db_norm;
        nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 100));  // Subtle grey
        nvgStrokeWidth(args.vg, 0.5f);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, zero_db_x, 0.5f);
        nvgLineTo(args.vg, zero_db_x, display_height - 0.5f);
        nvgStroke(args.vg);

        // Thin black separator line between stereo bars (50% opaque)
        nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 128));  // 50% opaque black
        nvgStrokeWidth(args.vg, 0.5f);  // Extremely thin line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 1.0f, display_height * 0.5f);  // Start at left edge, center height
        nvgLineTo(args.vg, display_width - 1.0f, display_height * 0.5f);  // End at right edge, center height
        nvgStroke(args.vg);
    }
};

struct VUMeterDisplay : widget::TransparentWidget {
    Module* module;

    // VU meter ballistic parameters (standard VU response)
    static constexpr float VU_ATTACK_MS = 5.0f;      // Fast attack for peaks
    static constexpr float VU_DECAY_MS = 300.0f;     // Standard 300ms VU ballistics
    static constexpr int VU_DECIMATION = 8;          // Update at 6kHz (48kHz/8 = 6kHz)

    // Visual parameters (inheriting RMS styling)
    float display_width = 88.0f;    // 4px clearance from each side (96-8=88)
    float display_height = 7.5f;    // Share 30px remaining height with 4 meters (30/4=7.5)

    // VU state
    float vu_left = 0.0f;
    float vu_right = 0.0f;
    float attack_coeff = 0.0f;
    float decay_coeff = 0.0f;
    int vu_decimation_counter = 0;

    // Peak hold state for VU
    float vu_peak_hold_left = 0.0f;
    float vu_peak_hold_right = 0.0f;
    float vu_peak_hold_timer_left = 0.0f;
    float vu_peak_hold_timer_right = 0.0f;
    static constexpr float VU_PEAK_HOLD_DECAY_TIME = 0.5f;

    // TC color scheme - Amber meter color (matching RMS and LED rings)
    uint8_t meter_r = 0xFF, meter_g = 0xC0, meter_b = 0x50;  // Amber
    uint8_t bg_r = 40, bg_g = 40, bg_b = 40;                  // Dark background

    VUMeterDisplay(Module* module) : module(module) {
        // Calculate VU ballistic coefficients based on sample rate
        updateCoefficients();
    }

    void reset() {
        // Reset all VU state
        vu_left = 0.0f;
        vu_right = 0.0f;
        vu_decimation_counter = 0;
        vu_peak_hold_left = 0.0f;
        vu_peak_hold_right = 0.0f;
        vu_peak_hold_timer_left = 0.0f;
        vu_peak_hold_timer_right = 0.0f;
    }

    void updateCoefficients() {
        if (!module) return;
        float sampleRate = APP->engine->getSampleRate();

        // Calculate exponential decay coefficients for VU ballistics
        attack_coeff = 1.0f - std::exp(-1000.0f / (VU_ATTACK_MS * sampleRate));
        decay_coeff = 1.0f - std::exp(-1000.0f / (VU_DECAY_MS * sampleRate));
    }

    void addStereoSample(float left, float right) {
        if (!module) return;

        // Decimation: Only process every VU_DECIMATION samples (6kHz update rate)
        vu_decimation_counter++;
        if (vu_decimation_counter < VU_DECIMATION) {
            return;
        }
        vu_decimation_counter = 0;

        // Convert to absolute values for VU measurement
        float left_abs = std::abs(left);
        float right_abs = std::abs(right);

        // VU ballistic response: fast attack, slow decay
        if (left_abs > vu_left) {
            vu_left += attack_coeff * (left_abs - vu_left);  // Fast attack
        } else {
            vu_left += decay_coeff * (left_abs - vu_left);   // Slow decay (300ms)
        }

        if (right_abs > vu_right) {
            vu_right += attack_coeff * (right_abs - vu_right);  // Fast attack
        } else {
            vu_right += decay_coeff * (right_abs - vu_right);   // Slow decay (300ms)
        }

        // Update peak hold
        if (vu_left > vu_peak_hold_left) {
            vu_peak_hold_left = vu_left;
            vu_peak_hold_timer_left = VU_PEAK_HOLD_DECAY_TIME;
        }
        if (vu_right > vu_peak_hold_right) {
            vu_peak_hold_right = vu_right;
            vu_peak_hold_timer_right = VU_PEAK_HOLD_DECAY_TIME;
        }

        // Decay peak hold timers (time delta per VU_DECIMATION samples)
        float delta_time = VU_DECIMATION / 48000.0f;
        vu_peak_hold_timer_left = std::max(0.0f, vu_peak_hold_timer_left - delta_time);
        vu_peak_hold_timer_right = std::max(0.0f, vu_peak_hold_timer_right - delta_time);

        // Decay peak hold to current value when timer expires
        if (vu_peak_hold_timer_left <= 0.0f) {
            vu_peak_hold_left = std::max(vu_peak_hold_left - delta_time * 10.0f, vu_left);
        }
        if (vu_peak_hold_timer_right <= 0.0f) {
            vu_peak_hold_right = std::max(vu_peak_hold_right - delta_time * 10.0f, vu_right);
        }
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Background bar (same styling as RMS)
        nvgFillColor(args.vg, nvgRGBA(bg_r, bg_g, bg_b, 255));
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, display_width, display_height, 2.0f);
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 80, 255));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        float left_db = (vu_left > 0.0001f) ?
            20.0f * std::log10(vu_left / 5.0f) : -60.0f;
        float right_db = (vu_right > 0.0001f) ?
            20.0f * std::log10(vu_right / 5.0f) : -60.0f;

        // Clamp to meter range (-60dB to +6dB)
        left_db = rack::math::clamp(left_db, -60.0f, 6.0f);
        right_db = rack::math::clamp(right_db, -60.0f, 6.0f);

        // Normalize to 0-1 range for bar width
        float left_norm = (left_db + 60.0f) / 66.0f;
        float right_norm = (right_db + 60.0f) / 66.0f;

        // Draw left channel bar (top half) with green gradient
        float bar_height = (display_height - 1.0f) * 0.5f;
        float left_width = (display_width - 2.0f) * left_norm;

        if (left_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint leftGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                    // Start point (left edge)
                1.0f + left_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, leftGradient);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f, left_width, bar_height);
            nvgFill(args.vg);
        }

        // Draw right channel bar (bottom half) with amber gradient
        float right_width = (display_width - 2.0f) * right_norm;

        if (right_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint rightGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                     // Start point (left edge)
                1.0f + right_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, rightGradient);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f + bar_height, right_width, bar_height);
            nvgFill(args.vg);
        }

        // Peak hold white lines
        float peak_hold_left_db = (vu_peak_hold_left > 0.0001f) ?
            20.0f * std::log10(vu_peak_hold_left / 5.0f) : -60.0f;
        float peak_hold_right_db = (vu_peak_hold_right > 0.0001f) ?
            20.0f * std::log10(vu_peak_hold_right / 5.0f) : -60.0f;

        peak_hold_left_db = rack::math::clamp(peak_hold_left_db, -60.0f, 6.0f);
        peak_hold_right_db = rack::math::clamp(peak_hold_right_db, -60.0f, 6.0f);

        float peak_hold_left_norm = (peak_hold_left_db + 60.0f) / 66.0f;
        float peak_hold_right_norm = (peak_hold_right_db + 60.0f) / 66.0f;

        if (vu_peak_hold_left > 0.0001f) {
            float peak_x = 1.0f + (display_width - 2.0f) * peak_hold_left_norm;
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, peak_x, 0.5f);
            nvgLineTo(args.vg, peak_x, 0.5f + bar_height);
            nvgStroke(args.vg);
        }

        if (vu_peak_hold_right > 0.0001f) {
            float peak_x = 1.0f + (display_width - 2.0f) * peak_hold_right_norm;
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, peak_x, 0.5f + bar_height);
            nvgLineTo(args.vg, peak_x, display_height - 0.5f);
            nvgStroke(args.vg);
        }

        // 0dB reference line (thin grey line behind bars)
        // 0dB is at 60dB into the 66dB range (-60 to +6), so normalized position is 60/66
        float zero_db_norm = 60.0f / 66.0f;
        float zero_db_x = 1.0f + (display_width - 2.0f) * zero_db_norm;
        nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 100));  // Subtle grey
        nvgStrokeWidth(args.vg, 0.5f);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, zero_db_x, 0.5f);
        nvgLineTo(args.vg, zero_db_x, display_height - 0.5f);
        nvgStroke(args.vg);

        // Thin black separator line between stereo bars (50% opaque)
        nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 128));  // 50% opaque black
        nvgStrokeWidth(args.vg, 0.5f);  // Extremely thin line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 1.0f, display_height * 0.5f);  // Start at left edge, center height
        nvgLineTo(args.vg, display_width - 1.0f, display_height * 0.5f);  // End at right edge, center height
        nvgStroke(args.vg);
    }
};

struct PPMMeterDisplay : widget::TransparentWidget {
    Module* module;

    // Studio-optimized PPM-style ballistics (NOT IEC 60268-10 compliant)
    // IEC Type I/II requires: 10ms integration, 1.5-2.5s return time
    // This implementation uses faster ballistics for studio input monitoring
    static constexpr float PPM_ATTACK_MS = 0.1f;     // Very fast attack (vs IEC 10ms integration)
    static constexpr float PPM_DECAY_MS = 50.0f;     // Studio-fast decay (vs IEC 1.5-2.5s return)
    static constexpr int PPM_DECIMATION = 8;         // Update at 6kHz (48kHz/8 = 6kHz)

    // Visual parameters (inheriting styling)
    float display_width = 88.0f;    // 4px clearance from each side (96-8=88)
    float display_height = 7.5f;    // Share 30px remaining height with 4 meters (30/4=7.5)

    // PPM state
    float ppm_left = 0.0f;
    float ppm_right = 0.0f;
    float peak_left = 0.0f;
    float peak_right = 0.0f;
    float attack_coeff = 0.0f;
    float decay_coeff = 0.0f;
    int ppm_decimation_counter = 0;

    // Peak hold state for PPM
    float ppm_peak_hold_left = 0.0f;
    float ppm_peak_hold_right = 0.0f;
    float ppm_peak_hold_timer_left = 0.0f;
    float ppm_peak_hold_timer_right = 0.0f;
    static constexpr float PPM_PEAK_HOLD_DECAY_TIME = 0.5f;  // 0.5 seconds

    // TC color scheme - Amber meter color (matching RMS, VU, and LED rings)
    uint8_t meter_r = 0xFF, meter_g = 0xC0, meter_b = 0x50;  // Amber
    uint8_t bg_r = 40, bg_g = 40, bg_b = 40;                  // Dark background

    PPMMeterDisplay(Module* module) : module(module) {
        updateCoefficients();
    }

    void reset() {
        // Reset all PPM state
        ppm_left = 0.0f;
        ppm_right = 0.0f;
        peak_left = 0.0f;
        peak_right = 0.0f;
        ppm_decimation_counter = 0;
        ppm_peak_hold_left = 0.0f;
        ppm_peak_hold_right = 0.0f;
        ppm_peak_hold_timer_left = 0.0f;
        ppm_peak_hold_timer_right = 0.0f;
    }

    void updateCoefficients() {
        if (!module) return;
        float sampleRate = APP->engine->getSampleRate();

        // Calculate exponential coefficients for PPM ballistics
        attack_coeff = 1.0f - std::exp(-1000.0f / (PPM_ATTACK_MS * sampleRate));
        decay_coeff = 1.0f - std::exp(-1000.0f / (PPM_DECAY_MS * sampleRate));
    }

    void addStereoSample(float left, float right) {
        if (!module) return;

        // Decimation: Only process every PPM_DECIMATION samples (6kHz update rate)
        ppm_decimation_counter++;
        if (ppm_decimation_counter < PPM_DECIMATION) {
            return;
        }
        ppm_decimation_counter = 0;

        // Convert to absolute values for PPM measurement
        float left_abs = std::abs(left);
        float right_abs = std::abs(right);

        // PPM ballistic response: very fast attack, slow controlled decay
        if (left_abs > ppm_left) {
            ppm_left += attack_coeff * (left_abs - ppm_left);  // Very fast attack
            peak_left = ppm_left;  // Track peak
        } else {
            ppm_left += decay_coeff * (left_abs - ppm_left);   // Controlled decay
        }

        if (right_abs > ppm_right) {
            ppm_right += attack_coeff * (right_abs - ppm_right);  // Very fast attack
            peak_right = ppm_right;  // Track peak
        } else {
            ppm_right += decay_coeff * (right_abs - ppm_right);   // Controlled decay
        }

        // Update PPM peak hold
        if (ppm_left > ppm_peak_hold_left) {
            ppm_peak_hold_left = ppm_left;
            ppm_peak_hold_timer_left = PPM_PEAK_HOLD_DECAY_TIME;
        }
        if (ppm_right > ppm_peak_hold_right) {
            ppm_peak_hold_right = ppm_right;
            ppm_peak_hold_timer_right = PPM_PEAK_HOLD_DECAY_TIME;
        }

        // Decay PPM peak hold timers
        ppm_peak_hold_timer_left = std::max(0.0f, ppm_peak_hold_timer_left - (PPM_DECIMATION / 48000.0f));
        ppm_peak_hold_timer_right = std::max(0.0f, ppm_peak_hold_timer_right - (PPM_DECIMATION / 48000.0f));

        // Decay PPM peak hold to current value when timer expires
        if (ppm_peak_hold_timer_left <= 0.0f) {
            ppm_peak_hold_left = std::max(ppm_peak_hold_left - (PPM_DECIMATION / 48000.0f) * 10.0f, ppm_left);
        }
        if (ppm_peak_hold_timer_right <= 0.0f) {
            ppm_peak_hold_right = std::max(ppm_peak_hold_right - (PPM_DECIMATION / 48000.0f) * 10.0f, ppm_right);
        }
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Background bar (same styling as RMS/VU)
        nvgFillColor(args.vg, nvgRGBA(bg_r, bg_g, bg_b, 255));
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, display_width, display_height, 2.0f);
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 80, 255));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        float left_db = (ppm_left > 0.0001f) ?
            20.0f * std::log10(ppm_left / 5.0f) : -60.0f;
        float right_db = (ppm_right > 0.0001f) ?
            20.0f * std::log10(ppm_right / 5.0f) : -60.0f;

        // Clamp to meter range (-60dB to +6dB)
        left_db = rack::math::clamp(left_db, -60.0f, 6.0f);
        right_db = rack::math::clamp(right_db, -60.0f, 6.0f);

        // Normalize to 0-1 range for bar width
        float left_norm = (left_db + 60.0f) / 66.0f;
        float right_norm = (right_db + 60.0f) / 66.0f;

        // Draw left channel bar (top half) with orange gradient
        float bar_height = (display_height - 1.0f) * 0.5f;
        float left_width = (display_width - 2.0f) * left_norm;

        if (left_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint leftGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                    // Start point (left edge)
                1.0f + left_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, leftGradient);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f, left_width, bar_height);
            nvgFill(args.vg);
        }

        // Draw right channel bar (bottom half) with amber gradient
        float right_width = (display_width - 2.0f) * right_norm;

        if (right_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint rightGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                     // Start point (left edge)
                1.0f + right_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, rightGradient);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f + bar_height, right_width, bar_height);
            nvgFill(args.vg);
        }

        // PPM Peak hold indicators (white lines)
        float ppm_peak_hold_left_db = (ppm_peak_hold_left > 0.0001f) ?
            20.0f * std::log10(ppm_peak_hold_left / 5.0f) : -60.0f;
        float ppm_peak_hold_right_db = (ppm_peak_hold_right > 0.0001f) ?
            20.0f * std::log10(ppm_peak_hold_right / 5.0f) : -60.0f;

        ppm_peak_hold_left_db = rack::math::clamp(ppm_peak_hold_left_db, -60.0f, 6.0f);
        ppm_peak_hold_right_db = rack::math::clamp(ppm_peak_hold_right_db, -60.0f, 6.0f);

        float ppm_peak_hold_left_norm = (ppm_peak_hold_left_db + 60.0f) / 66.0f;
        float ppm_peak_hold_right_norm = (ppm_peak_hold_right_db + 60.0f) / 66.0f;

        if (ppm_peak_hold_left > 0.0001f) {
            float peak_x = 1.0f + (display_width - 2.0f) * ppm_peak_hold_left_norm;
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, peak_x, 0.5f);
            nvgLineTo(args.vg, peak_x, 0.5f + bar_height);
            nvgStroke(args.vg);
        }

        if (ppm_peak_hold_right > 0.0001f) {
            float peak_x = 1.0f + (display_width - 2.0f) * ppm_peak_hold_right_norm;
            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, peak_x, 0.5f + bar_height);
            nvgLineTo(args.vg, peak_x, display_height - 0.5f);
            nvgStroke(args.vg);
        }

        // Thin black separator line between stereo bars (50% opaque)
        nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 128));  // 50% opaque black
        nvgStrokeWidth(args.vg, 0.5f);  // Extremely thin line
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 1.0f, display_height * 0.5f);  // Start at left edge, center height
        nvgLineTo(args.vg, display_width - 1.0f, display_height * 0.5f);  // End at right edge, center height
        nvgStroke(args.vg);

        // 0dB reference line (thin grey line behind bars)
        // 0dB is at 60dB into the 66dB range (-60 to +6), so normalized position is 60/66
        float zero_db_norm = 60.0f / 66.0f;
        float zero_db_x = 1.0f + (display_width - 2.0f) * zero_db_norm;
        nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 100));  // Subtle grey
        nvgStrokeWidth(args.vg, 0.5f);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, zero_db_x, 0.5f);
        nvgLineTo(args.vg, zero_db_x, display_height - 0.5f);
        nvgStroke(args.vg);
    }
};

enum AeFilterType {
    AeLOWPASS,   // For high cut filter
    AeHIGHPASS   // For low cut filter
};

template <typename T>
struct AeFilter {
    T x[2] = {};  // Input history
    T y[2] = {};  // Output history
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;  // Biquad coefficients - initialized to unity gain
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;  // Ensures safe operation before setCutoff() is called

    inline T process(const T& in) noexcept {
        T out = b0 * in + b1 * x[0] + b2 * x[1] - a1 * y[0] - a2 * y[1];

        // Shift delay line buffers
        x[1] = x[0];
        x[0] = in;
        y[1] = y[0];
        y[0] = out;

        return out;
    }

    void setCutoff(float f, float q, int type) {
        const float w0 = 2 * M_PI * f / APP->engine->getSampleRate();
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cs0 = std::cos(w0);

        switch (type) {
            case AeLOWPASS:  // High cut filter implementation
                a0 = 1 + alpha;
                b0 = (1 - cs0) / 2 / a0;
                b1 = (1 - cs0) / a0;
                b2 = (1 - cs0) / 2 / a0;
                a1 = (-2 * cs0) / a0;
                a2 = (1 - alpha) / a0;
                break;
            case AeHIGHPASS: // Low cut filter implementation
                a0 = 1 + alpha;
                b0 = (1 + cs0) / 2 / a0;
                b1 = -(1 + cs0) / a0;
                b2 = (1 + cs0) / 2 / a0;
                a1 = -2 * cs0 / a0;
                a2 = (1 - alpha) / a0;
                break;
        }
    }
};

// VCA with anti-pop smoothing
class ChanInVCA {
private:
    dsp::SlewLimiter gainSlewer;

    static constexpr float ANTIPOP_SLEW_RATE = 25.0f;

public:
    ChanInVCA() {
        gainSlewer.setRiseFall(ANTIPOP_SLEW_RATE, ANTIPOP_SLEW_RATE);
    }

    void prepare() {
        gainSlewer.reset();
    }

    float processGain(float input, float gainDb, float sampleTime, float cvGain = 1.0f) {
        float targetGain = std::pow(10.0f, gainDb / 20.0f) * cvGain;
        float smoothedGain = gainSlewer.process(sampleTime, targetGain);
        return input * smoothedGain;
    }
};

// Dual-channel filter system
class ChanInFilters {
private:
    AeFilter<float> highCutFilter[2];  // Left/Right channels
    AeFilter<float> lowCutFilter[2];   // Left/Right channels

    float lastHighCutFreq = -1.0f;
    float lastLowCutFreq = -1.0f;

public:
    void updateFiltersIfChanged(float highCutFreq, float lowCutFreq, bool forceUpdate = false) {
        if (highCutFreq != lastHighCutFreq || forceUpdate) {
            for (int ch = 0; ch < 2; ch++) {
                highCutFilter[ch].setCutoff(highCutFreq, 0.8f, AeLOWPASS);
            }
            lastHighCutFreq = highCutFreq;
        }

        if (lowCutFreq != lastLowCutFreq || forceUpdate) {
            for (int ch = 0; ch < 2; ch++) {
                lowCutFilter[ch].setCutoff(lowCutFreq, 0.8f, AeHIGHPASS);
            }
            lastLowCutFreq = lowCutFreq;
        }
    }

    void processFilters(float* leftSample, float* rightSample) {
        // Serial processing: Low cut → High cut
        *leftSample = highCutFilter[0].process(lowCutFilter[0].process(*leftSample));
        *rightSample = highCutFilter[1].process(lowCutFilter[1].process(*rightSample));
    }

    void onSampleRateChange() {
        lastHighCutFreq = -1.0f;
        lastLowCutFreq = -1.0f;
    }
};

struct ChanIn : Module, IChanInVuLevels {
    enum ParamIds {
        LEVEL_PARAM,      // -60dB to +6dB gain (hybrid range)
        HIGH_CUT_PARAM,   // 1kHz to 20kHz low-pass
        LOW_CUT_PARAM,    // 20Hz to 500Hz high-pass
        PHASE_PARAM,      // Phase invert button
        DISPLAY_ENABLE_PARAM,  // Enable/disable display visibility
        PARAMS_LEN
    };

    enum InputIds {
        LEFT_INPUT,
        RIGHT_INPUT,
        INPUTS_LEN
    };

    enum OutputIds {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightIds {
        PHASE_LIGHT,      // Phase invert indicator
        VU_LIGHTS_LEFT,   // Start of left VU meter lights (17 total)
        VU_LIGHTS_LEFT_LAST = VU_LIGHTS_LEFT + 17 - 1,
        VU_LIGHTS_RIGHT,  // Start of right VU meter lights (17 total)
        VU_LIGHTS_RIGHT_LAST = VU_LIGHTS_RIGHT + 17 - 1,
        LIGHTS_LEN
    };

    // Precomputed constants for filter CV processing (avoid runtime log2 calls)
    static constexpr float LPF_CENTER_LOG = 12.126748332105768f;  // (log2(1000) + log2(20000)) / 2
    static constexpr float HPF_CENTER_LOG = 6.643856189774725f;   // (log2(20) + log2(500)) / 2
    static constexpr float LPF_MIN_LOG = 9.965784284662087f;      // log2(1000)
    static constexpr float LPF_MAX_LOG = 14.287712379549449f;     // log2(20000)
    static constexpr float HPF_MIN_LOG = 4.321928094887363f;      // log2(20)
    static constexpr float HPF_MAX_LOG = 8.965784284662087f;      // log2(500)

    ChanInVCA leftVCA, rightVCA;
    ChanInFilters filters;
    dsp::ClockDivider filterUpdateDivider;
    dsp::ClockDivider lightDivider;  // LED update clock divider (update every 256 samples)

    float vuLevelsLeft[17];
    float vuLevelsRight[17];

    // VU levels in dB for MIDI feedback (audio thread only)
    float vuLevelL = -60.0f;
    float vuLevelR = -60.0f;

    // Cross-plugin C interface for VU levels
    ChanInVuInterface vuInterface;

    // Cross-plugin expander message (for C1 to read via leftExpander)
    CrossPluginExpanderMessage leftExpanderMsg;

    // Static wrapper functions for C interface
    static float cGetVuLevelL(void* module) {
        return static_cast<ChanIn*>(module)->vuLevelL;
    }
    static float cGetVuLevelR(void* module) {
        return static_cast<ChanIn*>(module)->vuLevelR;
    }

    // Thread safety for shutdown
    std::atomic<bool> isShuttingDown{false};

    // RMS metering display (atomic for cross-platform thread safety)
    std::atomic<RMSMeterDisplay*> rmsMeter{nullptr};

    // VU metering display (atomic for cross-platform thread safety)
    std::atomic<VUMeterDisplay*> vuMeter{nullptr};

    // PPM metering display (atomic for cross-platform thread safety)
    std::atomic<PPMMeterDisplay*> ppmMeter{nullptr};

    // Metering switch widget (atomic for cross-platform thread safety)
    std::atomic<MeteringSwitchWidget*> meteringSwitchWidget{nullptr};

    // Meter mode selection (0=RMS, 1=VU, 2=PPM)
    int activeMeterMode = 0;

    // Expander message buffers for CHAN-IN-CV communication
    ChanInExpanderMessage rightMessages[2];  // Double buffer for thread safety

    // CV-modulated filter frequencies (for bypass logic)
    float activeHighCutFreq = 20000.0f;
    float activeLowCutFreq = 20.0f;

    ChanIn() {
        // Initialize cross-plugin C interface
        vuInterface.version = CROSS_PLUGIN_INTERFACE_VERSION;
        vuInterface.getVuLevelL = cGetVuLevelL;
        vuInterface.getVuLevelR = cGetVuLevelR;

        // Initialize cross-plugin expander message for C1 access
        leftExpanderMsg.magic = CROSS_PLUGIN_MAGIC;
        leftExpanderMsg.interfaceType = CROSS_PLUGIN_INTERFACE_CHANIN;
        leftExpanderMsg.interfacePtr = &vuInterface;

        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(LEVEL_PARAM, -60.0f, 6.0f, 0.0f, "Input Gain", " dB");
        configParam(HIGH_CUT_PARAM, 1000.0f, 20000.0f, 20000.0f, "High Cut", " Hz");
        configParam(LOW_CUT_PARAM, 20.0f, 500.0f, 20.0f, "Low Cut", " Hz");
        configParam<PhaseParamQuantity>(PHASE_PARAM, 0.0f, 1.0f, 0.0f, "Phase Invert");
        configParam(DISPLAY_ENABLE_PARAM, 0.0f, 1.0f, 1.0f, "Display Visibility");  // Default ON

        configInput(LEFT_INPUT, "Left");
        configInput(RIGHT_INPUT, "Right (left normalled)");

        configOutput(LEFT_OUTPUT, "Left");
        configOutput(RIGHT_OUTPUT, "Right");

        // VCV Rack engine-level bypass (right-click menu)
        configBypass(LEFT_INPUT, LEFT_OUTPUT);
        configBypass(RIGHT_INPUT, RIGHT_OUTPUT);

        filterUpdateDivider.setDivision(16);
        lightDivider.setDivision(256);  // Update LEDs every 256 samples (187.5Hz at 48kHz)

        // Initialize VU meter arrays to dim state
        for (int i = 0; i < 17; ++i) {
            vuLevelsLeft[i] = 0.02f;
            vuLevelsRight[i] = 0.02f;
        }

        // Critical: Initialize sample rate dependencies first
        onSampleRateChange();

        filters.updateFiltersIfChanged(
            params[HIGH_CUT_PARAM].getValue(),  // 20000.0f default
            params[LOW_CUT_PARAM].getValue(),   // 20.0f default
            true  // forceUpdate = true ensures initialization regardless of cache state
        );

        // Initialize expander messages for CV expander (right side)
        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];

        // Initialize left expander for cross-plugin interface (C1 reads this)
        leftExpander.producerMessage = &leftExpanderMsg;
        leftExpander.consumerMessage = &leftExpanderMsg;  // Not used, but must be set
    }

    ~ChanIn() {
        // Safe destructor - prevent crashes on sudden exit
        isShuttingDown.store(true);

        // Null out widget pointers atomically to prevent dangling pointer access (cross-platform thread safety)
        rmsMeter.store(nullptr);
        vuMeter.store(nullptr);
        ppmMeter.store(nullptr);
        meteringSwitchWidget.store(nullptr);

        // Brief pause to let audio thread see the shutdown flag and null pointers
        std::this_thread::sleep_for(std::chrono::microseconds(100));

        // No complex operations, just ensure safe state
    }

    void onRandomize(const RandomizeEvent& e) override {
        (void)e;  // Suppress unused parameter warning
        // Disable randomize - do nothing
    }

    void onAdd() override {
        // Module added to rack - no special action needed
    }

    void onRemove() override {
        // Module removed from rack - no special action needed
    }

    void onSampleRateChange() override {
        // Reset VCA state
        leftVCA.prepare();
        rightVCA.prepare();

        // Reset filter cache to force recalculation
        filters.onSampleRateChange();

        filters.updateFiltersIfChanged(
            params[HIGH_CUT_PARAM].getValue(),
            params[LOW_CUT_PARAM].getValue(),
            true  // forceUpdate = true for sample rate changes
        );
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "activeMeterMode", json_integer(activeMeterMode));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* meterModeJ = json_object_get(rootJ, "activeMeterMode");
        if (meterModeJ) {
            activeMeterMode = json_integer_value(meterModeJ);
            // Widget state will be synced in syncMeterModeToWidget() called from widget
        }
    }

    // Called from widget after construction to sync saved state
    void syncMeterModeToWidget();

    // IChanInVuLevels interface implementation (inline for zero overhead)
    float getVuLevelL() const override {
        return vuLevelL;
    }

    float getVuLevelR() const override {
        return vuLevelR;
    }

    void updateVuMeter(float leftLevel, float rightLevel) {
        float leftDb = (leftLevel > 0.0001f) ? 20.0f * std::log10(leftLevel / 5.0f) : -80.0f;
        float rightDb = (rightLevel > 0.0001f) ? 20.0f * std::log10(rightLevel / 5.0f) : -80.0f;
        leftDb = rack::math::clamp(leftDb, -80.0f, 6.0f);
        rightDb = rack::math::clamp(rightDb, -80.0f, 6.0f);

        // Store VU levels for MIDI feedback
        vuLevelL = leftDb;
        vuLevelR = rightDb;

        // Custom LED threshold mapping (matching ChanOut)
        // -60dB to -24dB: LEDs 0-4 (9dB steps)
        // -24dB to -12dB: LEDs 5-7 (4dB steps)
        // -12dB to -6dB: LEDs 8-10 (2dB steps)
        // -6dB to 0dB: LEDs 11-13 (2dB steps)
        // 0dB to +6dB: LEDs 14-16 (2dB steps)
        static const float ledThresholds[17] = {
            -60.0f,  // LED 0  (green) - bottom LED
            -51.0f,  // LED 1  (green)
            -42.0f,  // LED 2  (green)
            -33.0f,  // LED 3  (green)
            -24.0f,  // LED 4  (green) -24dB label
            -20.0f,  // LED 5  (green)
            -16.0f,  // LED 6  (green)
            -12.0f,  // LED 7  (green) -12dB label
            -10.0f,  // LED 8  (green)
            -8.0f,   // LED 9  (green)
            -6.0f,   // LED 10 (green) -6dB label, last green LED
            -4.0f,   // LED 11 (yellow) first yellow LED
            -2.0f,   // LED 12 (yellow)
            0.0f,    // LED 13 (yellow) 0dB label, last yellow LED
            +2.0f,   // LED 14 (red) first red LED
            +4.0f,   // LED 15 (red)
            +6.0f    // LED 16 (red) +6dB label, top LED
        };

        for (int i = 0; i < 17; ++i) {
            vuLevelsLeft[i] = (leftDb >= ledThresholds[i]) ? 1.0f : 0.0f;
            vuLevelsRight[i] = (rightDb >= ledThresholds[i]) ? 1.0f : 0.0f;
        }

        for (int i = 0; i < 17; ++i) {
            lights[VU_LIGHTS_LEFT + i].setBrightness(vuLevelsLeft[i]);
            lights[VU_LIGHTS_RIGHT + i].setBrightness(vuLevelsRight[i]);
        }
    }

    void process(const ProcessArgs& args) override {
        // Thread safety: abort processing if module is shutting down
        if (isShuttingDown.load()) {
            // Zero outputs and return immediately
            outputs[LEFT_OUTPUT].setVoltage(0.0f);
            outputs[RIGHT_OUTPUT].setVoltage(0.0f);
            return;
        }

        // Check for CV expander and read CV modulation
        float levelCVMod = 0.0f;
        float hpfFreqCVMod = 0.0f;
        float lpfFreqCVMod = 0.0f;
        bool phaseInvertCV = false;

        if (rightExpander.module && rightExpander.module->model == modelChanInCV) {
            // Read from CI-X's consumer message (where CI-X wrote the data)
            ChanInExpanderMessage* msg = (ChanInExpanderMessage*)(rightExpander.module->leftExpander.consumerMessage);
            levelCVMod = msg->levelCV * 66.0f;  // Full parameter range: -60dB to +6dB
            hpfFreqCVMod = msg->lpfFreqCV;      // Swapped: read LPF message for HPF mod
            lpfFreqCVMod = msg->hpfFreqCV;      // Swapped: read HPF message for LPF mod
            phaseInvertCV = (msg->phaseInvertCV > 1.0f);
        }

        float leftIn = inputs[LEFT_INPUT].getVoltage();
        float rightIn = inputs[RIGHT_INPUT].isConnected() ?
            inputs[RIGHT_INPUT].getVoltage() : leftIn;

        float cvGain = 1.0f;

        if (filterUpdateDivider.process()) {
            float highCutLog, lowCutLog;

            // LPF: CV overrides knob (like Gain CV)
            if (lpfFreqCVMod != 0.0f) {
                // Use center of range + CV (1V/oct)
                highCutLog = LPF_CENTER_LOG + lpfFreqCVMod;
            } else {
                // Use knob value when CV disconnected
                highCutLog = std::log2(params[HIGH_CUT_PARAM].getValue());
            }

            // HPF: CV overrides knob (like Gain CV)
            if (hpfFreqCVMod != 0.0f) {
                // Use center of range + CV (1V/oct)
                lowCutLog = HPF_CENTER_LOG + hpfFreqCVMod;
            } else {
                // Use knob value when CV disconnected
                lowCutLog = std::log2(params[LOW_CUT_PARAM].getValue());
            }

            // Clamp to valid ranges and convert back
            highCutLog = clamp(highCutLog, LPF_MIN_LOG, LPF_MAX_LOG);
            lowCutLog = clamp(lowCutLog, HPF_MIN_LOG, HPF_MAX_LOG);

            activeHighCutFreq = std::pow(2.0f, highCutLog);
            activeLowCutFreq = std::pow(2.0f, lowCutLog);

            filters.updateFiltersIfChanged(activeHighCutFreq, activeLowCutFreq);
        }

        // Apply filters conditionally (like MindMeld MixMaster)
        // Only process filters when they're set to meaningful frequencies
        // Use CV-modulated frequencies for bypass check
        bool applyHighCut = (activeHighCutFreq < 20000.0f);  // Apply when set below 20kHz
        bool applyLowCut = (activeLowCutFreq > 20.0f);       // Apply when set above 20Hz

        if (applyHighCut || applyLowCut) {
            filters.processFilters(&leftIn, &rightIn);
        }

        // Apply phase invert (button OR CV)
        bool phaseInvert = (params[PHASE_PARAM].getValue() > 0.5f) || phaseInvertCV;
        if (phaseInvert) {
            leftIn = -leftIn;
            rightIn = -rightIn;
        }

        // Apply gain with CV modulation
        float gainDbBase = params[LEVEL_PARAM].getValue();
        float gainDb = clamp(gainDbBase + levelCVMod, -60.0f, 6.0f);
        float leftOut = leftVCA.processGain(leftIn, gainDb, args.sampleTime, cvGain);
        float rightOut = rightVCA.processGain(rightIn, gainDb, args.sampleTime, cvGain);

        // Update LEDs at reduced rate (every 256 samples = 187.5Hz at 48kHz)
        bool updateLights = lightDivider.process();
        if (updateLights) {
            updateVuMeter(std::abs(leftOut), std::abs(rightOut));
            lights[PHASE_LIGHT].setBrightness(phaseInvert ? 0.65f : 0.0f);
        }

        // Feed audio to active meter if display enabled, silence to all meters if display disabled for graceful decay
        bool displayEnabled = params[DISPLAY_ENABLE_PARAM].getValue() > 0.5f;

        // Use atomic load for cross-platform thread safety
        auto* rms = rmsMeter.load();
        if (rms) {
            if (activeMeterMode == 0 && displayEnabled) {
                rms->addStereoSample(leftOut, rightOut);
            } else {
                rms->addStereoSample(0.0f, 0.0f);  // Feed silence for natural decay
            }
        }
        auto* vu = vuMeter.load();
        if (vu) {
            if (activeMeterMode == 1 && displayEnabled) {
                vu->addStereoSample(leftOut, rightOut);
            } else {
                vu->addStereoSample(0.0f, 0.0f);  // Feed silence for natural decay
            }
        }
        auto* ppm = ppmMeter.load();
        if (ppm) {
            if (activeMeterMode == 2 && displayEnabled) {
                ppm->addStereoSample(leftOut, rightOut);
            } else {
                ppm->addStereoSample(0.0f, 0.0f);  // Feed silence for natural decay
            }
        }

        outputs[LEFT_OUTPUT].setVoltage(leftOut);
        outputs[RIGHT_OUTPUT].setVoltage(rightOut);
    }

    void setMeterMode(int mode) {
        if (mode >= 0 && mode <= 2) {
            activeMeterMode = mode;
            // Note: Inactive meters will gracefully decay naturally without new samples
        }
    }
};

// Metering switch widget with meter selection functionality
struct MeteringSwitchWidget : widget::TransparentWidget {
    Module* module;

    // Visual constants
    static constexpr float SWITCH_SIZE = 5.6f;  // 5.6px squares
    static constexpr float SWITCH_SPACING = 7.0f;  // 7px spacing between squares

    int currentMeterMode = 0;  // Default to first switch (RMS)

    MeteringSwitchWidget(Module* module) : module(module) {}

    void draw(const DrawArgs& args) override {
        drawMeteringSwitches(args);
    }

    void drawMeteringSwitches(const DrawArgs& args) {
        // Use direct pixel values
        float switchSizePx = SWITCH_SIZE;  // 5.6px
        float spacingPx = SWITCH_SPACING;  // 7px spacing

        // Draw 3 rectangles in upper left corner of widget area
        for (int i = 0; i < 3; i++) {
            float x = 2.0f + (i * spacingPx);  // Start 2px from widget left edge
            float y = 2.0f;  // 2px from widget top edge

            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, switchSizePx, switchSizePx, 1.0f);  // 1px rounded corners

            // Only draw outline, no fill
            nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 255));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            // Draw amber checkmark for active switch
            if (i == currentMeterMode) {
                // Use same amber as meter bars and LED rings
                nvgStrokeColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 255));
                nvgStrokeWidth(args.vg, 1.2f);
                nvgLineCap(args.vg, NVG_ROUND);

                // Draw checkmark: short line + longer line
                float centerX = x + switchSizePx * 0.5f;
                float centerY = y + switchSizePx * 0.5f;
                float size = switchSizePx * 0.3f;

                nvgBeginPath(args.vg);
                // Short line (bottom-left to center)
                nvgMoveTo(args.vg, centerX - size * 0.5f, centerY);
                nvgLineTo(args.vg, centerX - size * 0.1f, centerY + size * 0.4f);
                // Long line (center to top-right)
                nvgLineTo(args.vg, centerX + size * 0.6f, centerY - size * 0.3f);
                nvgStroke(args.vg);
            }
        }
    }

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            // Check if click is on any metering switch
            float switchSizePx = SWITCH_SIZE;
            float spacingPx = SWITCH_SPACING;

            for (int i = 0; i < 3; i++) {
                float x = 2.0f + (i * spacingPx);
                float y = 2.0f;

                if (e.pos.x >= x && e.pos.x <= x + switchSizePx &&
                    e.pos.y >= y && e.pos.y <= y + switchSizePx) {

                    currentMeterMode = i;
                    if (module) {
                        static_cast<ChanIn*>(module)->setMeterMode(i);
                    }
                    e.consume(this);
                    return;
                }
            }
        }
        TransparentWidget::onButton(e);
    }
};

// Define syncMeterModeToWidget after MeteringSwitchWidget is complete
void ChanIn::syncMeterModeToWidget() {
    auto* widget = meteringSwitchWidget.load();
    if (widget) {
        widget->currentMeterMode = activeMeterMode;
    }
}

// Dynamic dB readout widget - shows peak hold value of active meter
struct DynamicDbReadoutWidget : widget::TransparentWidget {
    ChanIn* module;

    DynamicDbReadoutWidget(ChanIn* m) : module(m) {}

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Amber color matching meter
        nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 200));
        nvgFontSize(args.vg, 7.0f);
        nvgFontFaceId(args.vg, APP->window->uiFont->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        // Get peak hold value based on active meter mode
        float peakDb = -60.0f;
        bool hasSignal = false;

        if (module->activeMeterMode == 0) {
            // RMS meter
            auto* rms = module->rmsMeter.load();
            if (rms) {
                float peak_left = rms->peak_hold_left;
                float peak_right = rms->peak_hold_right;
                float peak_max = std::max(peak_left, peak_right);

                if (peak_max > 0.0001f) {
                    peakDb = 20.0f * std::log10(peak_max / 5.0f);
                    peakDb = rack::math::clamp(peakDb, -60.0f, 6.0f);
                    hasSignal = true;
                }
            }
        } else if (module->activeMeterMode == 1) {
            // VU meter
            auto* vu = module->vuMeter.load();
            if (vu) {
                float peak_left = vu->vu_peak_hold_left;
                float peak_right = vu->vu_peak_hold_right;
                float peak_max = std::max(peak_left, peak_right);

                if (peak_max > 0.0001f) {
                    peakDb = 20.0f * std::log10(peak_max / 5.0f);
                    peakDb = rack::math::clamp(peakDb, -60.0f, 6.0f);
                    hasSignal = true;
                }
            }
        } else if (module->activeMeterMode == 2) {
            // PPM meter
            auto* ppm = module->ppmMeter.load();
            if (ppm) {
                float peak_left = ppm->ppm_peak_hold_left;
                float peak_right = ppm->ppm_peak_hold_right;
                float peak_max = std::max(peak_left, peak_right);

                if (peak_max > 0.0001f) {
                    peakDb = 20.0f * std::log10(peak_max / 5.0f);
                    peakDb = rack::math::clamp(peakDb, -60.0f, 6.0f);
                    hasSignal = true;
                }
            }
        }

        // Display value or infinity symbol
        if (hasSignal) {
            char dbText[16];
            snprintf(dbText, sizeof(dbText), "%.1f dB", peakDb);
            nvgText(args.vg, box.size.x / 2.0f, 0.0f, dbText, NULL);
        } else {
            nvgText(args.vg, box.size.x / 2.0f, 0.0f, "\u221E", NULL);
        }
    }
};

struct ChanInWidget : ModuleWidget {
    // Store meter widgets for visibility control
    RMSMeterDisplay* rmsMeter = nullptr;
    VUMeterDisplay* vuMeter = nullptr;
    PPMMeterDisplay* ppmMeter = nullptr;
    MeteringSwitchWidget* meteringSwitchWidget = nullptr;


    ChanInWidget(ChanIn* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ChanIn.svg")));

        addChild(createWidget<ScrewBlack>(Vec(0, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

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
        inLabel->box.pos = Vec(35, 335);  // Centered at X=35 under IN box
        inLabel->box.size = Vec(20, 10);
        addChild(inLabel);

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
        outLabel->box.pos = Vec(85, 335);  // Centered at X=85 under OUT box
        outLabel->box.size = Vec(20, 10);
        addChild(outLabel);

        struct TitleLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 18.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "CHAN-IN", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "CHAN-IN", NULL);
            }
        };

        TitleLabel* titleLabel = new TitleLabel();
        titleLabel->box.pos = Vec(60, 10);
        titleLabel->box.size = Vec(104, 20);
        addChild(titleLabel);

        // Twisted Cable logo - using shared widget
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::FULL, module);
        tcLogo->box.pos = Vec(60, 355);
        tcLogo->box.size = Vec(42, 20);
        addChild(tcLogo);

        initVuMeterLights();

        struct ControlLabel : Widget {
            std::string text;
            ControlLabel(std::string t) : text(t) {}
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, text.c_str(), NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, text.c_str(), NULL);
            }
        };

        ControlLabel* hpfLabel = new ControlLabel("HPF");
        hpfLabel->box.pos = Vec(85, 149);
        hpfLabel->box.size = Vec(40, 10);
        addChild(hpfLabel);

        ControlLabel* lpfLabel = new ControlLabel("LPF");
        lpfLabel->box.pos = Vec(85, 199);
        lpfLabel->box.size = Vec(40, 10);
        addChild(lpfLabel);

        ControlLabel* phaseLabel = new ControlLabel("PHASE");
        phaseLabel->box.pos = Vec(85, 249);
        phaseLabel->box.size = Vec(40, 10);
        addChild(phaseLabel);

        ControlLabel* gainLabel = new ControlLabel("GAIN");
        gainLabel->box.pos = Vec(35, 249);
        gainLabel->box.size = Vec(40, 10);
        addChild(gainLabel);

        struct VuMinLabel : Widget {
            void draw(const DrawArgs& args) override {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));

                // Draw dash at 5.0f
                nvgFontSize(args.vg, 5.0f);
                float dashWidth = nvgText(args.vg, 0, box.size.y / 2, "-", NULL);

                // Draw "60" at 6.0f
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, dashWidth, box.size.y / 2, "60", NULL);
            }
        };

        VuMinLabel* vuMinLabel = new VuMinLabel();
        vuMinLabel->box.pos = Vec(43, 189);
        vuMinLabel->box.size = Vec(20, 8);
        addChild(vuMinLabel);

        struct Vu24Label : Widget {
            void draw(const DrawArgs& args) override {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));

                // Draw dash at 5.0f
                nvgFontSize(args.vg, 5.0f);
                float dashWidth = nvgText(args.vg, 0, box.size.y / 2, "-", NULL);

                // Draw "24" at 6.0f
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, dashWidth, box.size.y / 2, "24", NULL);
            }
        };

        Vu24Label* vu24Label = new Vu24Label();
        vu24Label->box.pos = Vec(43, 168);
        vu24Label->box.size = Vec(20, 8);
        addChild(vu24Label);

        struct Vu12Label : Widget {
            void draw(const DrawArgs& args) override {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));

                // Draw dash at 5.0f
                nvgFontSize(args.vg, 5.0f);
                float dashWidth = nvgText(args.vg, 0, box.size.y / 2, "-", NULL);

                // Draw "12" at 6.0f
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, dashWidth, box.size.y / 2, "12", NULL);
            }
        };

        Vu12Label* vu12Label = new Vu12Label();
        vu12Label->box.pos = Vec(43, 152);
        vu12Label->box.size = Vec(20, 8);
        addChild(vu12Label);

        struct Vu6Label : Widget {
            void draw(const DrawArgs& args) override {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));

                // Draw dash at 5.0f
                nvgFontSize(args.vg, 5.0f);
                float dashWidth = nvgText(args.vg, 0, box.size.y / 2, "-", NULL);

                // Draw "6" at 6.0f
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, dashWidth, box.size.y / 2, "6", NULL);
            }
        };

        Vu6Label* vu6Label = new Vu6Label();
        vu6Label->box.pos = Vec(43, 135);
        vu6Label->box.size = Vec(20, 8);
        addChild(vu6Label);

        struct Vu0Label : Widget {
            void draw(const DrawArgs& args) override {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));

                // Draw dash at 5.0f
                nvgFontSize(args.vg, 5.0f);
                float dashWidth = nvgText(args.vg, 0, box.size.y / 2, "-", NULL);

                // Draw "0" at 6.0f
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, dashWidth, box.size.y / 2, "0", NULL);
            }
        };

        Vu0Label* vu0Label = new Vu0Label();
        vu0Label->box.pos = Vec(43, 119);
        vu0Label->box.size = Vec(20, 8);
        addChild(vu0Label);

        struct VuPlus6Label : Widget {
            void draw(const DrawArgs& args) override {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));

                // Draw dash at 5.0f
                nvgFontSize(args.vg, 5.0f);
                float dashWidth = nvgText(args.vg, 0, box.size.y / 2, "-", NULL);

                // Draw "6" at 6.0f
                nvgFontSize(args.vg, 6.0f);
                nvgText(args.vg, dashWidth, box.size.y / 2, "6", NULL);
            }
        };

        VuPlus6Label* vuPlus6Label = new VuPlus6Label();
        vuPlus6Label->box.pos = Vec(43, 103);
        vuPlus6Label->box.size = Vec(20, 8);
        addChild(vuPlus6Label);

        addParam(createParamCentered<C1Knob280>(Vec(35, 225), module, ChanIn::LEVEL_PARAM));
        LedRingOverlay* levelRing = new LedRingOverlay(module, ChanIn::LEVEL_PARAM);
        levelRing->box.pos = Vec(35 - 25, 225 - 25);
        addChild(levelRing);

        addParam(createParamCentered<C1Knob280>(Vec(85, 125), module, ChanIn::HIGH_CUT_PARAM));
        LedRingOverlay* highCutRing = new LedRingOverlay(module, ChanIn::HIGH_CUT_PARAM);
        highCutRing->box.pos = Vec(85 - 25, 125 - 25);
        addChild(highCutRing);

        addParam(createParamCentered<C1Knob280>(Vec(85, 175), module, ChanIn::LOW_CUT_PARAM));
        LedRingOverlay* lowCutRing = new LedRingOverlay(module, ChanIn::LOW_CUT_PARAM);
        lowCutRing->box.pos = Vec(85 - 25, 175 - 25);
        addChild(lowCutRing);

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

        C1WhiteRoundButton* phaseButton = createParamCentered<C1WhiteRoundButton>(Vec(85, 225), module, ChanIn::PHASE_PARAM);
        phaseButton->getLight()->module = module;
        if (module) {
            phaseButton->getLight()->firstLightId = ChanIn::PHASE_LIGHT;
        }
        addParam(phaseButton);

        // I/O jacks centered under encoder columns (X=35 for IN, X=85 for OUT)
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(35, 283), module, ChanIn::LEFT_INPUT));
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(35, 313), module, ChanIn::RIGHT_INPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(85, 283), module, ChanIn::LEFT_OUTPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(85, 313), module, ChanIn::RIGHT_OUTPUT));

        // Add RMS meter in audio analysis area (leaving space for switches at top)
        if (module) {
            rmsMeter = new RMSMeterDisplay(module);
            rmsMeter->box.pos = Vec(16, 56);    // 5.4px clearance (matching C1COMP)
            rmsMeter->box.size = Vec(88, 7.5f);
            addChild(rmsMeter);

            // Connect meter to module for sample feeding (atomic store for cross-platform thread safety)
            static_cast<ChanIn*>(module)->rmsMeter.store(rmsMeter);

            // Add VU meter below RMS meter
            vuMeter = new VUMeterDisplay(module);
            vuMeter->box.pos = Vec(16, 63.5f);    // 7.5px below RMS meter
            vuMeter->box.size = Vec(88, 7.5f);
            addChild(vuMeter);

            // Connect VU meter to module for sample feeding (atomic store for cross-platform thread safety)
            static_cast<ChanIn*>(module)->vuMeter.store(vuMeter);

            // Add PPM meter below VU meter
            ppmMeter = new PPMMeterDisplay(module);
            ppmMeter->box.pos = Vec(16, 71.0f);   // 7.5px below VU meter
            ppmMeter->box.size = Vec(88, 7.5f);
            addChild(ppmMeter);

            // Connect PPM meter to module for sample feeding (atomic store for cross-platform thread safety)
            static_cast<ChanIn*>(module)->ppmMeter.store(ppmMeter);

            // Initialize: Reset all inactive meters to prevent stuck values on startup
            // Default mode is 0 (RMS), so reset VU and PPM meters
            vuMeter->reset();
            ppmMeter->reset();

            // Add metering switch widget in upper left corner of metering display
            this->meteringSwitchWidget = new MeteringSwitchWidget(module);
            this->meteringSwitchWidget->box.pos = Vec(14, 43);
            this->meteringSwitchWidget->box.size = Vec(23, 12);    // 3 switches: 2px margin + 3×5.6px switches + 2×1.4px gaps + 2px margin
            addChild(this->meteringSwitchWidget);

            // Connect widget to module for state synchronization (atomic store for cross-platform thread safety)
            static_cast<ChanIn*>(module)->meteringSwitchWidget.store(this->meteringSwitchWidget);
            // Sync saved meter mode state to widget
            static_cast<ChanIn*>(module)->syncMeterModeToWidget();

            // Meter type label - displays current meter name
            struct MeterTypeLabel : Widget {
                ChanIn* module;
                MeterTypeLabel(ChanIn* m) : module(m) {}
                void draw(const DrawArgs& args) override {
                    if (!module) return;

                    const char* meterNames[3] = {"RMS", "VU", "PPM"};
                    int mode = module->activeMeterMode;
                    mode = clamp(mode, 0, 2);

                    nvgFontSize(args.vg, 6.0f);
                    nvgFontFaceId(args.vg, APP->window->uiFont->handle);
                    nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 200));  // Amber
                    nvgText(args.vg, 0, box.size.y / 2, meterNames[mode], NULL);
                }
            };

            MeterTypeLabel* meterLabel = new MeterTypeLabel(module);
            meterLabel->box.pos = Vec(39, 45);  // 2px gap after 23px switch box (14+23+2=39)
            meterLabel->box.size = Vec(50, 6);
            addChild(meterLabel);

            // Add single rectangle switch in upper right corner
            struct SimpleSwitch : widget::OpaqueWidget {
                ChanIn* module = nullptr;
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

                    // Get display enable state from module parameter
                    bool displayOn = module ? (module->params[ChanIn::DISPLAY_ENABLE_PARAM].getValue() > 0.5f) : true;

                    // Fill with amber when display is enabled
                    if (displayOn) {
                        nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, (int)(255 * opacity)));
                        nvgFill(args.vg);
                    }

                    nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, (int)(255 * opacity)));
                    nvgStrokeWidth(args.vg, 0.5f);
                    nvgStroke(args.vg);

                    // Draw cross (X) when display is disabled
                    if (!displayOn) {
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
                        // Toggle the display enable parameter
                        float currentValue = module->params[ChanIn::DISPLAY_ENABLE_PARAM].getValue();
                        module->params[ChanIn::DISPLAY_ENABLE_PARAM].setValue(currentValue > 0.5f ? 0.0f : 1.0f);

                        e.consume(this);
                    }
                }
            };

            SimpleSwitch* simpleSwitch = new SimpleSwitch();
            simpleSwitch->module = module;
            simpleSwitch->box.pos = Vec(96, 43);
            simpleSwitch->box.size = Vec(12, 12);
            addChild(simpleSwitch);

            // dB reference labels (5.0f font size matching ChanOut)
            struct DbLabel : Widget {
                const char* text;
                DbLabel(const char* t) : text(t) {}
                void draw(const DrawArgs& args) override {
                    nvgFontSize(args.vg, 5.0f);
                    nvgFontFaceId(args.vg, APP->window->uiFont->handle);
                    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 180));
                    nvgText(args.vg, 0, 0, text, NULL);
                }
            };

            // Dynamic dB readout - centered below meters showing peak hold value
            DynamicDbReadoutWidget* dbReadout = new DynamicDbReadoutWidget(module);
            dbReadout->box.pos = Vec(16, 85);  // Centered on X-axis (16 + 88/2 = 60), centered in empty space
            dbReadout->box.size = Vec(88, 10);
            addChild(dbReadout);

        }
    }

    void initVuMeterLights() {
        float vuStartY = 107.0f;
        float vuLedSpacing = 5.4f;
        float vuLeftX = 32.0f;
        float vuRightX = 38.0f;

        for (int i = 0; i < 17; ++i) {
            float yPos = vuStartY + (16 - i) * vuLedSpacing;

            if (i < 11) {
                auto* leftLight = createLightCentered<TinyLight<GreenLight>>(
                    Vec(vuLeftX, yPos), module, ChanIn::VU_LIGHTS_LEFT + i);
                addChild(leftLight);
                auto* rightLight = createLightCentered<TinyLight<GreenLight>>(
                    Vec(vuRightX, yPos), module, ChanIn::VU_LIGHTS_RIGHT + i);
                addChild(rightLight);
            } else if (i < 14) {
                auto* leftLight = createLightCentered<TinyLight<YellowLight>>(
                    Vec(vuLeftX, yPos), module, ChanIn::VU_LIGHTS_LEFT + i);
                addChild(leftLight);
                auto* rightLight = createLightCentered<TinyLight<YellowLight>>(
                    Vec(vuRightX, yPos), module, ChanIn::VU_LIGHTS_RIGHT + i);
                addChild(rightLight);
            } else {
                auto* leftLight = createLightCentered<TinyLight<RedLight>>(
                    Vec(vuLeftX, yPos), module, ChanIn::VU_LIGHTS_LEFT + i);
                addChild(leftLight);
                auto* rightLight = createLightCentered<TinyLight<RedLight>>(
                    Vec(vuRightX, yPos), module, ChanIn::VU_LIGHTS_RIGHT + i);
                addChild(rightLight);
            }
        }
    }
};

} // namespace

Model* modelChanIn = createModel<ChanIn, ChanInWidget>("ChanIn");