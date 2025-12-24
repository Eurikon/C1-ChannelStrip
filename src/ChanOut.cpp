#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include "../shared/include/CrossPluginInterface.h"
#include "ChanOutCleanEngine.hpp"
#include "ChanOutAPIEngine.hpp"
#include "ChanOutNeveEngine.hpp"
#include "ChanOutDangerousEngine.hpp"
#include "ebur128.h"
#include <dsp/ringbuffer.hpp>

// Custom ParamQuantity for Dim button with ON/OFF labels
struct DimParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "ON";
    }
};

// Custom ParamQuantity for Mute button with ON/OFF labels
struct MuteParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "ON";
    }
};

namespace {

// Expander message struct for ChanOutCV (CHO-X) communication
struct ChanOutExpanderMessage {
    float gainCV;      // -1.0 to +1.0 (attenuated)
    float panCV;       // -1.0 to +1.0 (attenuated)
    float driveCV;     // -1.0 to +1.0 (attenuated)
    float characterCV; // -1.0 to +1.0 (attenuated)
};

// Forward declarations
struct LUFSMeterDisplay;
struct ChanOut;
struct Control1;

// C1 Custom Knob - 85% scaled TC encoder with graphite/anthracite palette
struct C1Knob : RoundKnob {
    C1Knob() {
        // Load background only - use bg SVG for both to see static background
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

// LED Ring Overlay Widget - 15 amber LEDs with 80° bottom gap (Console1 style)
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

        // LED ring specifications (matching Console1MIDI)
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

// LUFS Meter Display - EBU R128 compliant using libebur128
struct LUFSMeterDisplay : widget::TransparentWidget {
    Module* module;

    // Thread safety: flag to prevent access during destruction
    std::atomic<bool> isDestroying{false};

    // libebur128 state
    ebur128_state* ebur_state = nullptr;
    float current_sample_rate = 0.0f;

    // Decimation for efficient processing
    static constexpr int LUFS_DECIMATION = 2048;  // Process every 2048 samples
    int decimation_counter = 0;
    std::vector<float> sample_buffer;  // Interleaved L/R buffer

    // Display values - momentary loudness (400ms window)
    float momentary_lufs = -70.0f;
    float lufs_peak_hold = -70.0f;
    float lufs_peak_hold_timer = 0.0f;
    static constexpr float LUFS_PEAK_HOLD_DECAY_TIME = 0.5f;

    // Signal detection (epsilon threshold)
    float signal_level_smoothed = 0.0f;  // Smoothed absolute signal level
    static constexpr float SIGNAL_EPSILON = 0.0001f;  // 0.1mV threshold

    // Visual parameters
    float display_width = 88.0f;
    float display_height = 7.5f;
    uint8_t meter_r = 0xFF, meter_g = 0xC0, meter_b = 0x50;  // Amber
    uint8_t bg_r = 40, bg_g = 40, bg_b = 40;                  // Dark background

    LUFSMeterDisplay(Module* module) : module(module) {
        sample_buffer.reserve(LUFS_DECIMATION * 2);  // Stereo interleaved
        initEbur128();
    }

    ~LUFSMeterDisplay() {
        // Set flag BEFORE destroying ebur_state to prevent race condition
        isDestroying.store(true);
        if (ebur_state) {
            ebur128_destroy(&ebur_state);
        }
    }

    void initEbur128() {
        if (!module) return;

        float sample_rate = APP->engine->getSampleRate();
        if (sample_rate == current_sample_rate && ebur_state) return;

        // Destroy old state if sample rate changed
        if (ebur_state) {
            ebur128_destroy(&ebur_state);
        }

        // Initialize with MODE_M for momentary loudness (400ms window)
        ebur_state = ebur128_init(
            2,                              // Stereo
            (unsigned long)sample_rate,
            EBUR128_MODE_M                  // Momentary mode (400ms sliding window)
        );

        if (ebur_state) {
            ebur128_set_channel(ebur_state, 0, EBUR128_LEFT);
            ebur128_set_channel(ebur_state, 1, EBUR128_RIGHT);
            current_sample_rate = sample_rate;
        }
    }

    void reset() {
        momentary_lufs = -70.0f;
        lufs_peak_hold = -70.0f;
        lufs_peak_hold_timer = 0.0f;
        decimation_counter = 0;
        sample_buffer.clear();
        signal_level_smoothed = 0.0f;  // Reset signal detection

        // Reinitialize ebur128 state to clear history
        if (ebur_state) {
            ebur128_destroy(&ebur_state);
            ebur_state = nullptr;
        }
        initEbur128();
    }

    void addStereoSample(float left, float right) {
        if (!module) return;
        if (isDestroying.load()) return;  // Prevent access during destruction

        initEbur128();
        if (!ebur_state) return;

        // Track signal level for epsilon threshold check (smoothed)
        float absMax = std::max(std::abs(left), std::abs(right));
        signal_level_smoothed += (absMax - signal_level_smoothed) * 0.05f;  // Smooth with VU_SMOOTH constant

        // Scale VCV Rack ±5V to libebur128 ±1.0f (0dBFS)
        // VCV: 5V = 0dB, libebur128: 1.0f = 0dBFS
        sample_buffer.push_back(left * 0.2f);
        sample_buffer.push_back(right * 0.2f);

        decimation_counter++;
        if (decimation_counter < LUFS_DECIMATION) {
            return;
        }
        decimation_counter = 0;

        // Process accumulated samples
        if (sample_buffer.size() >= 2) {
            ebur128_add_frames_float(
                ebur_state,
                sample_buffer.data(),
                sample_buffer.size() / 2  // Frame count (stereo pairs)
            );
        }
        sample_buffer.clear();

        // Get momentary loudness (400ms sliding window)
        double loudness;
        int result = ebur128_loudness_momentary(ebur_state, &loudness);
        if (result == EBUR128_SUCCESS) {
            momentary_lufs = (float)loudness;
        }
        // If not enough data yet (< 400ms), keep previous value

        // Update peak hold
        if (momentary_lufs > lufs_peak_hold) {
            lufs_peak_hold = momentary_lufs;
            lufs_peak_hold_timer = LUFS_PEAK_HOLD_DECAY_TIME;
        }

        // Decay peak hold
        float decay_step = (LUFS_DECIMATION / current_sample_rate);
        lufs_peak_hold_timer = std::max(0.0f, lufs_peak_hold_timer - decay_step);

        if (lufs_peak_hold_timer <= 0.0f) {
            lufs_peak_hold = std::max(
                lufs_peak_hold - decay_step * 20.0f,
                momentary_lufs
            );
        }
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Background bar (same styling as RMS/VU/PPM)
        nvgFillColor(args.vg, nvgRGBA(bg_r, bg_g, bg_b, 255));
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, display_width, display_height, 2.0f);
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 80, 255));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // LUFS range: -60 LUFS to 0 LUFS
        float lufs_clamped = rack::math::clamp(momentary_lufs, -60.0f, 0.0f);

        // Normalize to 0-1 range for bar width (LUFS scale)
        float lufs_norm = (lufs_clamped + 60.0f) / 60.0f;  // 60dB range for LUFS

        // Draw dual channel bars with purple gradient (same value for both channels)
        float bar_height = (display_height - 1.0f) * 0.5f;
        float lufs_width = (display_width - 2.0f) * lufs_norm;

        if (lufs_width > 1.0f) {
            // Create horizontal gradient from 30% amber (left) to 100% amber (right)
            NVGpaint lufsGradient = nvgLinearGradient(args.vg,
                1.0f, 0.0f,                    // Start point (left edge)
                1.0f + lufs_width, 0.0f,       // End point (right edge of bar)
                nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),  // 30% amber
                nvgRGBA(meter_r, meter_g, meter_b, 200));                        // 100% amber

            nvgFillPaint(args.vg, lufsGradient);

            // Draw both channel bars (top and bottom)
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 1.0f, 0.5f, lufs_width, bar_height);
            nvgRect(args.vg, 1.0f, 0.5f + bar_height, lufs_width, bar_height);
            nvgFill(args.vg);
        }

        // LUFS Peak hold indicator (white line)
        if (lufs_peak_hold > -60.0f) {
            float lufs_peak_hold_clamped = rack::math::clamp(lufs_peak_hold, -60.0f, 0.0f);
            float lufs_peak_hold_norm = (lufs_peak_hold_clamped + 60.0f) / 60.0f;  // 60dB range for LUFS
            float peak_x = 1.0f + (display_width - 2.0f) * lufs_peak_hold_norm;

            nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 180));  // White
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            // Draw vertical line across both channels for LUFS
            nvgMoveTo(args.vg, peak_x, 0.5f);
            nvgLineTo(args.vg, peak_x, display_height - 0.5f);
            nvgStroke(args.vg);
        }

        // 0 LUFS reference line (thin grey line behind bars)
        // 0 LUFS is at the far right (100% position) in -60 to 0 LUFS range
        float zero_lufs_x = 1.0f + (display_width - 2.0f) * 1.0f;  // Full right position
        nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 100));  // Subtle grey
        nvgStrokeWidth(args.vg, 0.5f);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, zero_lufs_x, 0.5f);
        nvgLineTo(args.vg, zero_lufs_x, display_height - 0.5f);
        nvgStroke(args.vg);

        // Numeric LUFS readout (centered, below meter, showing peak hold value)
        nvgFontSize(args.vg, 6.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, nvgRGBA(meter_r, meter_g, meter_b, 200));

        // Check epsilon threshold - only display LUFS value if signal is present
        bool hasSignal = (signal_level_smoothed >= SIGNAL_EPSILON);

        if (hasSignal) {
            // Display numeric LUFS value
            char lufsText[16];
            snprintf(lufsText, sizeof(lufsText), "%.1f LUFS", lufs_peak_hold);
            nvgText(args.vg, display_width / 2.0f, display_height + 0.5f, lufsText, NULL);
        } else {
            // Display infinity symbol when no signal
            nvgText(args.vg, display_width / 2.0f, display_height + 0.5f, "\u221E", NULL);
        }
    }
};

// Character Engine Switch Widget - 4 rectangle switches for engine selection (matching C1COMP style)
struct CharacterEngineSwitchWidget : widget::TransparentWidget {
    Module* module;
    int* currentEngineType = nullptr;

    static constexpr float SWITCH_SIZE = 5.6f;
    static constexpr float SWITCH_SPACING = 7.0f;

    CharacterEngineSwitchWidget(Module* m, int* typePtr)
        : module(m), currentEngineType(typePtr) {}

    void draw(const DrawArgs& args) override {
        float switchSizePx = SWITCH_SIZE;
        float spacingPx = SWITCH_SPACING;

        // Draw 4 rectangles for Standard, 2520, 8816, DM2plus
        for (int i = 0; i < 4; i++) {
            float x = 2.0f + (i * spacingPx);
            float y = 2.0f;

            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, switchSizePx, switchSizePx, 1.0f);

            nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 255));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            // Draw amber checkmark for active engine type
            if (currentEngineType && i == *currentEngineType) {
                nvgStrokeColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 255));
                nvgStrokeWidth(args.vg, 1.2f);
                nvgLineCap(args.vg, NVG_ROUND);

                float centerX = x + switchSizePx * 0.5f;
                float centerY = y + switchSizePx * 0.5f;
                float size = switchSizePx * 0.3f;

                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, centerX - size * 0.5f, centerY);
                nvgLineTo(args.vg, centerX - size * 0.1f, centerY + size * 0.4f);
                nvgLineTo(args.vg, centerX + size * 0.6f, centerY - size * 0.3f);
                nvgStroke(args.vg);
            }
        }
    }

    void setEngineTypeAndOversampling(int engineType);

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && currentEngineType && module) {
            float switchSizePx = SWITCH_SIZE;
            float spacingPx = SWITCH_SPACING;

            for (int i = 0; i < 4; i++) {
                float x = 2.0f + (i * spacingPx);
                float y = 2.0f;

                if (e.pos.x >= x && e.pos.x <= x + switchSizePx &&
                    e.pos.y >= y && e.pos.y <= y + switchSizePx) {

                    setEngineTypeAndOversampling(i);
                    e.consume(this);
                    return;
                }
            }
        }
        TransparentWidget::onButton(e);
    }
};

// CHAN-OUT Module - Output stage with drive, character, and pan
struct ChanOut : rack::engine::Module, IChanOutMode {
    enum ParamIds {
        DRIVE_PARAM,
        CHARACTER_PARAM,
        PAN_PARAM,
        VOLUME_PARAM,
        DIM_BUTTON_PARAM,
        MUTE_BUTTON_PARAM,
        DISPLAY_ENABLE_PARAM,
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
        VU_LIGHTS_LEFT,   // Start of left VU meter lights (17 total)
        VU_LIGHTS_LEFT_LAST = VU_LIGHTS_LEFT + 17 - 1,
        VU_LIGHTS_RIGHT,  // Start of right VU meter lights (17 total)
        VU_LIGHTS_RIGHT_LAST = VU_LIGHTS_RIGHT + 17 - 1,
        DIM_BUTTON_LIGHT,
        MUTE_BUTTON_LIGHT,
        LIGHTS_LEN
    };

    // Module state
    int outputMode = 0;         // 0 = Master Output, 1 = Channel Output
    int characterEngine = 0;    // 0 = Standard, 1 = 2520, 2 = 8816, 3 = DM2plus
    int oversampleFactor = 8;   // 2520 engine oversampling: 1, 2, 4, or 8 (engine switch sets 8x)
    int neveOversampleFactor = 8; // 8816 engine oversampling: 1, 2, 4, or 8 (engine switch sets 8x)
    int dangerousOversampleFactor = 8; // DM2plus engine oversampling: 1, 2, 4, or 8 (engine switch sets 8x)

    // Expander message buffers for ChanOutCV (CHO-X) communication
    ChanOutExpanderMessage rightMessages[2];  // Double buffer for thread safety

    // Character engines
    ChanOutClean::CleanEngine cleanEngine;
    ChanOutAPI::APIEngine apiEngine;
    ChanOutNeve::NeveEngine neveEngine;
    ChanOutDangerous::DangerousEngine dangerousEngine;

    dsp::ClockDivider lightDivider;  // LED update clock divider (update every 256 samples)

    // VU metering
    float vuLevelL = 0.0f;
    float vuLevelR = 0.0f;
    float vuSmoothL = 0.0f;
    float vuSmoothR = 0.0f;
    static constexpr float VU_SMOOTH = 0.05f;  // Smoothing factor

    // Cross-plugin C interface
    ChanOutInterface outInterface;

    // Cross-plugin expander message (for C1 to read via leftExpander)
    CrossPluginExpanderMessage leftExpanderMsg;

    // Static wrapper functions for C interface
    static int cGetOutputMode(void* module) {
        return static_cast<ChanOut*>(module)->outputMode;
    }
    static float cGetVuLevelL(void* module) {
        return static_cast<ChanOut*>(module)->vuLevelL;
    }
    static float cGetVuLevelR(void* module) {
        return static_cast<ChanOut*>(module)->vuLevelR;
    }

    // Peak hold for VU meter LEDs
    float vuPeakLevelL = -60.0f;      // Peak level in dB (L channel)
    float vuPeakLevelR = -60.0f;      // Peak level in dB (R channel)
    float vuPeakTimerL = 0.0f;        // Hold timer in seconds (L)
    float vuPeakTimerR = 0.0f;        // Hold timer in seconds (R)

    // LUFS metering (thread-safe atomic pointer)
    std::atomic<LUFSMeterDisplay*> lufsMeter{nullptr};

    // Shutdown flag for safe widget pointer access
    std::atomic<bool> isShuttingDown{false};

    // Thread-safe pending oversampling factor changes (UI thread → Audio thread)
    std::atomic<int> pending2520OversampleFactor{-1};
    std::atomic<int> pendingNeveOversampleFactor{-1};
    std::atomic<int> pendingDangerousOversampleFactor{-1};

    // Goniometer sample buffer (thread-safe lock-free ring buffer)
    struct GoniometerSample {
        float left;
        float right;
    };
    dsp::RingBuffer<GoniometerSample, 256> goniometerBuffer;

    // Volume anti-pop slew limiter
    dsp::SlewLimiter volumeSlewer;
    static constexpr float VOLUME_SLEW_RATE = 10.0f;  // Slower than ChanIn for smoother MIDI control

    // Pan anti-pop slew limiter
    dsp::SlewLimiter panSlewer;
    static constexpr float PAN_SLEW_RATE = 10.0f;

    // Mute anti-pop slew limiter
    float muteGain = 1.0f;  // Current mute gain (slewed)
    static constexpr float MUTE_SLEW_RATE = 125.0f;  // Units per second (from MindMeld antipopSlewFast)

    // Dim anti-pop slew limiter
    float dimGainSmoothed = 1.0f;  // Current dim gain multiplier (slewed)
    static constexpr float DIM_SLEW_RATE = 125.0f;

    // Dim configuration (adjustable via context menu)
    float dimGain = 0.25119f;  // Linear gain (default: -12 dB)
    float dimGainIntegerDB = 0.25119f;  // Rounded to integer dB, then back to linear

    // Trim configuration (adjustable via context menu)
    float trimGain = 1.0f;  // Linear gain (default: 0 dB)

    // Peak hold configuration (adjustable via context menu)
    bool peakHoldEnabled = true;           // Enable/disable peak hold
    float peakHoldTime = 1.5f;             // Hold time in seconds (default 1.5s)
    float peakFallRate = 24.0f;            // Fall rate in dB/second (default 24dB/s)

    // Helper to calculate dimGainIntegerDB from dimGain
    float calcDimGainIntegerDB(float gain) {
        float integerDB = std::round(20.0f * std::log10(gain));
        return std::pow(10.0f, integerDB / 20.0f);
    }

    ChanOut() {
        // Initialize cross-plugin C interface
        outInterface.version = CROSS_PLUGIN_INTERFACE_VERSION;
        outInterface.getOutputMode = cGetOutputMode;
        outInterface.getVuLevelL = cGetVuLevelL;
        outInterface.getVuLevelR = cGetVuLevelR;

        // Initialize cross-plugin expander message for C1 access
        leftExpanderMsg.magic = CROSS_PLUGIN_MAGIC;
        leftExpanderMsg.interfaceType = CROSS_PLUGIN_INTERFACE_CHANOUT;
        leftExpanderMsg.interfacePtr = &outInterface;

        // Initialize left expander for cross-plugin interface (C1 reads this)
        leftExpander.producerMessage = &leftExpanderMsg;
        leftExpander.consumerMessage = &leftExpanderMsg;  // Not used, but must be set

        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Parameter configuration (Master Output mode by default)
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
        configParam(CHARACTER_PARAM, 0.f, 1.f, 0.5f, "Character", "%", 0.f, 100.f);
        configParam(PAN_PARAM, -1.f, 1.f, 0.f, "Pan");
        configParam(VOLUME_PARAM, -60.f, 0.f, 0.f, "Master Level", " dB");  // Master mode: -60dB to 0dB
        configParam<DimParamQuantity>(DIM_BUTTON_PARAM, 0.f, 1.f, 0.f, "Dim");
        configParam<MuteParamQuantity>(MUTE_BUTTON_PARAM, 0.f, 1.f, 0.f, "Mute");
        configParam(DISPLAY_ENABLE_PARAM, 0.f, 1.f, 1.f, "Display Enable");

        // I/O configuration
        configInput(LEFT_INPUT, "Left");
        configInput(RIGHT_INPUT, "Right");
        configOutput(LEFT_OUTPUT, "Left");
        configOutput(RIGHT_OUTPUT, "Right");

        // VCV Rack engine-level bypass (right-click menu)
        configBypass(LEFT_INPUT, LEFT_OUTPUT);
        configBypass(RIGHT_INPUT, RIGHT_OUTPUT);

        lightDivider.setDivision(256);  // Update LEDs every 256 samples (187.5Hz at 48kHz)

        // Initialize engines
        cleanEngine.setOutputMode(outputMode);
        apiEngine.setOutputMode(outputMode);
        neveEngine.setOutputMode(outputMode);
        dangerousEngine.setOutputMode(outputMode);

        // Initialize volume slew limiter
        volumeSlewer.setRiseFall(VOLUME_SLEW_RATE, VOLUME_SLEW_RATE);

        // Initialize pan slew limiter
        panSlewer.setRiseFall(PAN_SLEW_RATE, PAN_SLEW_RATE);

        // Initialize dim gain
        dimGainIntegerDB = calcDimGainIntegerDB(dimGain);
    }

    ~ChanOut() {
        // Signal shutdown and null widget pointer for thread safety
        isShuttingDown.store(true);
        lufsMeter.store(nullptr);
    }

    void onRandomize(const RandomizeEvent& e) override {
        (void)e;  // Suppress unused parameter warning
        // Disable randomize - do nothing
    }

    void onReset() override {
        // Reset all parameters to defaults
        Module::onReset();

        // Reset context menu settings to defaults
        outputMode = 0;      // Master Output
        characterEngine = 0; // Standard
        dimGain = 0.25119f;  // -12 dB
        dimGainIntegerDB = calcDimGainIntegerDB(dimGain);
        trimGain = 1.0f;     // 0 dB

        // Reset peak hold state
        vuPeakLevelL = -60.0f;
        vuPeakLevelR = -60.0f;
        vuPeakTimerL = 0.0f;
        vuPeakTimerR = 0.0f;

        // Update engine with default mode
        setOutputMode(outputMode);
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        cleanEngine.setSampleRate(sr);
        apiEngine.setSampleRate(sr);
        neveEngine.setSampleRate(sr);
        dangerousEngine.setSampleRate(sr);
    }

    void setOutputMode(int mode) {
        outputMode = mode;

        // Update parameter ranges
        if (mode == 0) {
            // Master Output: -60dB to 0dB
            paramQuantities[VOLUME_PARAM]->minValue = -60.0f;
            paramQuantities[VOLUME_PARAM]->maxValue = 0.0f;
            paramQuantities[VOLUME_PARAM]->name = "Master Level";
        } else {
            // Channel Output: -60dB to +6dB
            paramQuantities[VOLUME_PARAM]->minValue = -60.0f;
            paramQuantities[VOLUME_PARAM]->maxValue = 6.0f;
            paramQuantities[VOLUME_PARAM]->name = "Output Level";
        }

        // Clamp current value if needed
        float currentVol = params[VOLUME_PARAM].getValue();
        if (mode == 0 && currentVol > 0.0f) {
            params[VOLUME_PARAM].setValue(0.0f);
        }

        // Notify character engines of mode change
        cleanEngine.setOutputMode(mode);
        apiEngine.setOutputMode(mode);
        neveEngine.setOutputMode(mode);
        dangerousEngine.setOutputMode(mode);
    }

    // IChanOutMode interface implementation (inline for zero overhead)
    int getOutputMode() const override {
        return outputMode;
    }

    float getVuLevelL() const override {
        return vuLevelL;
    }

    float getVuLevelR() const override {
        return vuLevelR;
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "outputMode", json_integer(outputMode));
        json_object_set_new(rootJ, "characterEngine", json_integer(characterEngine));
        json_object_set_new(rootJ, "oversampleFactor", json_integer(oversampleFactor));
        json_object_set_new(rootJ, "neveOversampleFactor", json_integer(neveOversampleFactor));
        json_object_set_new(rootJ, "dangerousOversampleFactor", json_integer(dangerousOversampleFactor));
        json_object_set_new(rootJ, "dimGain", json_real(dimGain));
        json_object_set_new(rootJ, "trimGain", json_real(trimGain));
        json_object_set_new(rootJ, "peakHoldEnabled", json_boolean(peakHoldEnabled));
        json_object_set_new(rootJ, "peakHoldTime", json_real(peakHoldTime));
        json_object_set_new(rootJ, "peakFallRate", json_real(peakFallRate));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* outputModeJ = json_object_get(rootJ, "outputMode");
        if (outputModeJ)
            setOutputMode(json_integer_value(outputModeJ));

        json_t* characterEngineJ = json_object_get(rootJ, "characterEngine");
        if (characterEngineJ)
            characterEngine = json_integer_value(characterEngineJ);

        json_t* oversampleFactorJ = json_object_get(rootJ, "oversampleFactor");
        if (oversampleFactorJ) {
            oversampleFactor = json_integer_value(oversampleFactorJ);
            apiEngine.engineL.setOversampleFactor(oversampleFactor);
            apiEngine.engineR.setOversampleFactor(oversampleFactor);
        }

        json_t* neveOversampleFactorJ = json_object_get(rootJ, "neveOversampleFactor");
        if (neveOversampleFactorJ) {
            neveOversampleFactor = json_integer_value(neveOversampleFactorJ);
            neveEngine.setOversampleFactor(neveOversampleFactor);
        }

        json_t* dangerousOversampleFactorJ = json_object_get(rootJ, "dangerousOversampleFactor");
        if (dangerousOversampleFactorJ) {
            dangerousOversampleFactor = json_integer_value(dangerousOversampleFactorJ);
            dangerousEngine.setOversampleFactor(dangerousOversampleFactor);
        }

        json_t* dimGainJ = json_object_get(rootJ, "dimGain");
        if (dimGainJ) {
            dimGain = json_number_value(dimGainJ);
            dimGainIntegerDB = calcDimGainIntegerDB(dimGain);
        }

        json_t* trimGainJ = json_object_get(rootJ, "trimGain");
        if (trimGainJ)
            trimGain = json_number_value(trimGainJ);

        json_t* peakHoldEnabledJ = json_object_get(rootJ, "peakHoldEnabled");
        if (peakHoldEnabledJ)
            peakHoldEnabled = json_boolean_value(peakHoldEnabledJ);

        json_t* peakHoldTimeJ = json_object_get(rootJ, "peakHoldTime");
        if (peakHoldTimeJ)
            peakHoldTime = json_real_value(peakHoldTimeJ);

        json_t* peakFallRateJ = json_object_get(rootJ, "peakFallRate");
        if (peakFallRateJ)
            peakFallRate = json_real_value(peakFallRateJ);
    }

    void process(const ProcessArgs& args) override {
        // Check mute state and apply anti-pop slew limiting
        bool muted = params[MUTE_BUTTON_PARAM].getValue() > 0.5f;
        float targetMuteGain = muted ? 0.0f : 1.0f;

        // Slew limiter: gradually move muteGain toward target to prevent clicks
        // Clamp the change rate to MUTE_SLEW_RATE units per second
        float maxChange = MUTE_SLEW_RATE * args.sampleTime;
        if (targetMuteGain > muteGain) {
            muteGain = std::min(targetMuteGain, muteGain + maxChange);
        } else if (targetMuteGain < muteGain) {
            muteGain = std::max(targetMuteGain, muteGain - maxChange);
        }

        // Read inputs (stereo with mono fallback)
        float left = inputs[LEFT_INPUT].getVoltage();
        float right = inputs[RIGHT_INPUT].isConnected() ? inputs[RIGHT_INPUT].getVoltage() : left;

        // Read CV modulation from CHO-X expander (if connected)
        float gainCVMod = 0.0f;
        float panCVMod = 0.0f;
        float driveCVMod = 0.0f;
        float characterCVMod = 0.0f;

        if (rightExpander.module && rightExpander.module->model == modelChanOutCV) {
            ChanOutExpanderMessage* msg = (ChanOutExpanderMessage*)(rightExpander.module->leftExpander.consumerMessage);
            gainCVMod = msg->gainCV * 66.0f;       // ±66dB range (covers both Master -60/0dB and Channel -60/+6dB)
            panCVMod = msg->panCV;                 // -1.0 to +1.0 (full bipolar pan range)
            driveCVMod = msg->driveCV;             // -1.0 to +1.0 (0-100%)
            characterCVMod = msg->characterCV;     // -1.0 to +1.0 (0-100%)
        }

        // Get parameters with CV modulation
        float drive = clamp(params[DRIVE_PARAM].getValue() + driveCVMod, 0.0f, 1.0f);
        float character = clamp(params[CHARACTER_PARAM].getValue() + characterCVMod, 0.0f, 1.0f);
        float panTarget = clamp(params[PAN_PARAM].getValue() + panCVMod, -1.0f, 1.0f);
        float volumeDB = clamp(params[VOLUME_PARAM].getValue() + gainCVMod, -60.0f, 6.0f);  // Full range covers both modes

        // Apply pan with slew limiting (equal power law) - BEFORE character engine
        float pan = panSlewer.process(args.sampleTime, panTarget);
        applyPan(left, right, pan);

        // Apply volume with slew limiting - BEFORE character engine
        float targetVolumeLinear = std::pow(10.0f, volumeDB / 20.0f);
        float volumeLinear = volumeSlewer.process(args.sampleTime, targetVolumeLinear);
        left *= volumeLinear;
        right *= volumeLinear;

        // Apply pending oversampling factor changes (thread-safe: deferred from UI thread)
        int pending2520 = pending2520OversampleFactor.load();
        if (pending2520 > 0) {
            apiEngine.engineL.setOversampleFactor(pending2520);
            apiEngine.engineR.setOversampleFactor(pending2520);
            pending2520OversampleFactor.store(-1);
        }

        int pendingNeve = pendingNeveOversampleFactor.load();
        if (pendingNeve > 0) {
            neveEngine.setOversampleFactor(pendingNeve);
            pendingNeveOversampleFactor.store(-1);
        }

        int pendingDangerous = pendingDangerousOversampleFactor.load();
        if (pendingDangerous > 0) {
            dangerousEngine.setOversampleFactor(pendingDangerous);
            pendingDangerousOversampleFactor.store(-1);
        }

        // Character engine processing (includes final clipping to ±10V)
        // This ensures output never exceeds VCV Rack voltage standards
        switch (characterEngine) {
            case 0: // Standard
                cleanEngine.process(left, right, drive);
                break;
            case 1: // 2520
                apiEngine.process(left, right, drive, character);
                break;
            case 2: // 8816
                neveEngine.process(left, right, drive, character);
                break;
            case 3: // DM2plus
                dangerousEngine.process(left, right, drive, character);
                break;
        }

        // Feed goniometer when display is enabled
        // Only push if buffer has capacity (no blocking)
        if (goniometerBuffer.capacity() > 0) {
            bool displayOn = params[DISPLAY_ENABLE_PARAM].getValue() > 0.5f;
            if (displayOn) {
                goniometerBuffer.push({left, right});
            } else {
                // Feed silence to clear display when disabled
                goniometerBuffer.push({0.0f, 0.0f});
            }
        }

        // Apply mute with anti-pop slew limiting
        left *= muteGain;
        right *= muteGain;

        // Apply dim with anti-pop slew limiting
        bool dimmed = params[DIM_BUTTON_PARAM].getValue() > 0.5f;
        float targetDimGain = dimmed ? dimGainIntegerDB : 1.0f;

        // Slew limiter: gradually move dimGainSmoothed toward target to prevent clicks
        float maxDimChange = DIM_SLEW_RATE * args.sampleTime;
        if (targetDimGain > dimGainSmoothed) {
            dimGainSmoothed = std::min(targetDimGain, dimGainSmoothed + maxDimChange);
        } else if (targetDimGain < dimGainSmoothed) {
            dimGainSmoothed = std::max(targetDimGain, dimGainSmoothed - maxDimChange);
        }

        left *= dimGainSmoothed;
        right *= dimGainSmoothed;

        // Apply trim (final gain stage after all processing)
        left *= trimGain;
        right *= trimGain;

        // Update LEDs at reduced rate (every 256 samples = 187.5Hz at 48kHz)
        bool updateLights = lightDivider.process();

        // VU metering (post-processing, pre-output)
        // Shows final output levels after all processing including trim
        updateVuMeters(left, right, updateLights);

        // LUFS metering (same point as VU meter)
        // Always feed actual signal for accurate loudness measurement
        // Thread-safe access to widget pointer
        if (!isShuttingDown.load()) {
            auto* meter = lufsMeter.load();
            if (meter) {
                meter->addStereoSample(left, right);
            }
        }

        // Write outputs (already clipped to ±10V by character engine)
        outputs[LEFT_OUTPUT].setVoltage(left);
        outputs[RIGHT_OUTPUT].setVoltage(right);
    }

    // Mode-dependent panning
    void applyPan(float& left, float& right, float pan) {
        if (pan == 0.0f) {
            // Center: no adjustment
            return;
        }

        // Convert pan (-1 to +1) to angle (0 to π/2)
        float angle = (pan + 1.0f) * 0.5f * float(M_PI_2);

        float panL, panR;

        if (outputMode == 0) {
            // Master Output mode: Linear panning (0dB)
            // Simple linear crossfade, no gain compensation
            panL = std::cos(angle);
            panR = std::sin(angle);
        } else {
            // Channel Output mode: Equal power panning (-3dB)
            // Constant perceived loudness with √2 boost
            panL = std::cos(angle) * std::sqrt(2.0f);
            panR = std::sin(angle) * std::sqrt(2.0f);
        }

        left *= panL;
        right *= panR;
    }

    // VU meter update
    void updateVuMeters(float left, float right, bool updateLights) {
        // Convert to absolute values
        float absL = std::abs(left);
        float absR = std::abs(right);

        // Smooth with exponential averaging
        vuSmoothL += (absL - vuSmoothL) * VU_SMOOTH;
        vuSmoothR += (absR - vuSmoothR) * VU_SMOOTH;

        // Convert to dB (5V = 0dBFS reference)
        vuLevelL = (vuSmoothL > 0.0001f) ? 20.0f * std::log10(vuSmoothL / 5.0f) : -60.0f;
        vuLevelR = (vuSmoothR > 0.0001f) ? 20.0f * std::log10(vuSmoothR / 5.0f) : -60.0f;

        // Clamp to VU range (-60dB to +6dB)
        vuLevelL = clamp(vuLevelL, -60.0f, 6.0f);
        vuLevelR = clamp(vuLevelR, -60.0f, 6.0f);

        // Update peak hold (if enabled)
        if (peakHoldEnabled) {
            // Capture new peaks
            if (vuLevelL > vuPeakLevelL) {
                vuPeakLevelL = vuLevelL;
                vuPeakTimerL = peakHoldTime;
            }
            if (vuLevelR > vuPeakLevelR) {
                vuPeakLevelR = vuLevelR;
                vuPeakTimerR = peakHoldTime;
            }

            // Decay hold timers
            float sampleTime = 1.0f / APP->engine->getSampleRate();
            vuPeakTimerL = std::max(0.0f, vuPeakTimerL - sampleTime);
            vuPeakTimerR = std::max(0.0f, vuPeakTimerR - sampleTime);

            // Fall after hold time expires
            if (vuPeakTimerL <= 0.0f) {
                vuPeakLevelL = std::max(vuPeakLevelL - (peakFallRate * sampleTime), vuLevelL);
            }
            if (vuPeakTimerR <= 0.0f) {
                vuPeakLevelR = std::max(vuPeakLevelR - (peakFallRate * sampleTime), vuLevelR);
            }
        }

        // Update LED lights at reduced rate (17 per channel)
        if (updateLights) {
            updateVuLights();
        }
    }

    void updateVuLights() {
        // MindMeld epsilon: don't show VUs below 0.1mV (prevents bottom LED staying lit)
        static constexpr float epsilon = 0.0001f;

        // Map -60dB to +6dB range to 17 LEDs with proper color boundaries
        // Green LEDs (0-10): -60dB to -6dB
        // Yellow LEDs (11-13): -6dB to 0dB
        // Red LEDs (14-16): 0dB to +6dB

        // Define explicit thresholds for each LED based on panel markings
        // Using 5V = 0dB reference (VCV Rack audio standard)
        // -60dB to -24dB: LEDs 0-4 (9dB steps)
        // -24dB to -12dB: LEDs 4-7 (4dB steps)
        // -12dB to -6dB: LEDs 7-10 (2dB steps)
        // -6dB to 0dB: LEDs 10-13 (2dB steps)
        // 0dB to +6dB: LEDs 13-16 (2dB steps)
        static const float ledThresholds[17] = {
            -60.0f,  // LED 0  (green) - bottom LED (0.005V)
            -51.0f,  // LED 1  (green) (0.014V)
            -42.0f,  // LED 2  (green) (0.040V)
            -33.0f,  // LED 3  (green) (0.112V)
            -24.0f,  // LED 4  (green) (0.316V) -24dB label
            -20.0f,  // LED 5  (green) (0.500V)
            -16.0f,  // LED 6  (green) (0.794V)
            -12.0f,  // LED 7  (green) (1.259V) -12dB label
            -10.0f,  // LED 8  (green) (1.585V)
            -8.0f,   // LED 9  (green) (1.995V)
            -6.0f,   // LED 10 (green) (2.512V) -6dB label, last green LED
            -4.0f,   // LED 11 (yellow) (3.162V) first yellow LED
            -2.0f,   // LED 12 (yellow) (3.981V)
            0.0f,    // LED 13 (yellow) (5.012V) 0dB label, last yellow LED
            +2.0f,   // LED 14 (red) (6.310V) first red LED
            +4.0f,   // LED 15 (red) (7.943V)
            +6.0f    // LED 16 (red) (10.000V) +6dB label, top LED
        };

        // Check epsilon threshold before displaying any LEDs (MindMeld approach)
        bool showLeftVu = (vuSmoothL >= epsilon);
        bool showRightVu = (vuSmoothR >= epsilon);

        for (int i = 0; i < 17; i++) {
            // Left channel - only show if above epsilon threshold
            lights[VU_LIGHTS_LEFT + i].setBrightness(
                (showLeftVu && vuLevelL >= ledThresholds[i]) ? 1.0f : 0.0f
            );

            // Right channel - only show if above epsilon threshold
            lights[VU_LIGHTS_RIGHT + i].setBrightness(
                (showRightVu && vuLevelR >= ledThresholds[i]) ? 1.0f : 0.0f
            );
        }

        // Render peak hold LED (single top LED per channel)
        if (peakHoldEnabled) {
            // Find which LED index corresponds to peak level
            int peakLedL = -1;
            int peakLedR = -1;

            for (int i = 16; i >= 0; i--) {
                if (vuPeakLevelL >= ledThresholds[i]) {
                    peakLedL = i;
                    break;
                }
            }
            for (int i = 16; i >= 0; i--) {
                if (vuPeakLevelR >= ledThresholds[i]) {
                    peakLedR = i;
                    break;
                }
            }

            // Light peak LED at full brightness (overrides normal VU)
            if (peakLedL >= 0 && vuPeakLevelL > -59.0f) {
                lights[VU_LIGHTS_LEFT + peakLedL].setBrightness(1.0f);
            }
            if (peakLedR >= 0 && vuPeakLevelR > -59.0f) {
                lights[VU_LIGHTS_RIGHT + peakLedR].setBrightness(1.0f);
            }
        }

        // Update button lights
        lights[DIM_BUTTON_LIGHT].setBrightness(params[DIM_BUTTON_PARAM].getValue());
        lights[MUTE_BUTTON_LIGHT].setBrightness(params[MUTE_BUTTON_PARAM].getValue());
    }
};

// Goniometer Display - Stereo field vectorscope (L vs R visualization)
// Defined after ChanOut struct so it can access ChanOut::GoniometerSample
struct GoniometerDisplay : widget::TransparentWidget {
    ChanOut* module;

    // Visual parameters
    float display_width = 88.0f;    // 4px clearance from each side (96-8=88)
    float display_height = 22.5f;   // Increased by 3px from top

    // TC color scheme
    uint8_t bg_r = 40, bg_g = 40, bg_b = 40;  // Dark background
    NVGcolor traceColor = nvgRGBA(0xFF, 0xC0, 0x50, 180);  // Amber with transparency
    NVGcolor gridColor = nvgRGBA(100, 100, 100, 100);      // Dim grid

    GoniometerDisplay(ChanOut* m) : module(m) {}

    void draw(const DrawArgs& args) override {
        // Background
        nvgFillColor(args.vg, nvgRGBA(bg_r, bg_g, bg_b, 255));
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, display_width, display_height, 2.0f);
        nvgFill(args.vg);

        // Border
        nvgStrokeColor(args.vg, nvgRGBA(80, 80, 80, 255));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, display_width, display_height, 2.0f);
        nvgStroke(args.vg);

        if (!module) return;

        // Center point
        float centerX = display_width / 2.0f;
        float centerY = display_height / 2.0f;

        // Draw center crosshair grid
        nvgStrokeColor(args.vg, gridColor);
        nvgStrokeWidth(args.vg, 0.5f);
        nvgBeginPath(args.vg);
        // Vertical center line
        nvgMoveTo(args.vg, centerX, 1.0f);
        nvgLineTo(args.vg, centerX, display_height - 1.0f);
        // Horizontal center line
        nvgMoveTo(args.vg, 1.0f, centerY);
        nvgLineTo(args.vg, display_width - 1.0f, centerY);
        nvgStroke(args.vg);

        // Read samples from ring buffer
        const int maxSamples = 128;  // Number of samples to display
        ChanOut::GoniometerSample samples[maxSamples];
        int sampleCount = 0;

        // Read available samples (up to maxSamples)
        while (!module->goniometerBuffer.empty() && sampleCount < maxSamples) {
            samples[sampleCount++] = module->goniometerBuffer.shift();
        }

        if (sampleCount < 2) return;  // Need at least 2 points to draw

        // Independent X/Y scaling to fill rectangular display
        // X: 95% of width for ±5V, Y: 95% of height for ±5V
        float scaleX = (display_width / 2.0f) * 0.95f / 5.0f;  // Horizontal scaling
        float scaleY = (display_height / 2.0f) * 0.95f / 5.0f; // Vertical scaling

        // Draw trace as individual dots (scatter plot style)
        const float dotRadius = 0.5f;  // 0.5px radius dots

        for (int i = 0; i < sampleCount; i++) {
            float left = samples[i].left;
            float right = samples[i].right;

            // Transform to screen coordinates
            // L → X axis (horizontal), R → Y axis (vertical, inverted for screen coords)
            float screenX = centerX + (left * scaleX);
            float screenY = centerY - (right * scaleY);

            // Clamp to display bounds
            screenX = clamp(screenX, 1.0f, display_width - 1.0f);
            screenY = clamp(screenY, 1.0f, display_height - 1.0f);

            // Calculate fade based on age (newer = more opaque)
            float age = (float)i / (float)sampleCount;  // 0.0 (old) to 1.0 (new)
            int alpha = 80 + (int)(100.0f * age);  // 80 to 180 alpha

            // Draw individual dot at this sample position
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, screenX, screenY, dotRadius);
            nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, alpha));
            nvgFill(args.vg);
        }
    }
};

// Implementation of CharacterEngineSwitchWidget::setEngineTypeAndOversampling
void CharacterEngineSwitchWidget::setEngineTypeAndOversampling(int engineType) {
    if (!currentEngineType || !module) return;

    // Set engine type
    *currentEngineType = engineType;

    // Set oversampling to 2x for 2520, 8816, and DM2plus engines when switch is clicked (default)
    ChanOut* chanOut = dynamic_cast<ChanOut*>(module);
    if (!chanOut) return;

    if (engineType == 1) {  // 2520 engine - default 2x
        chanOut->oversampleFactor = 2;
        chanOut->apiEngine.engineL.setOversampleFactor(2);
        chanOut->apiEngine.engineR.setOversampleFactor(2);
    } else if (engineType == 2) {  // 8816 engine - default 2x
        chanOut->neveOversampleFactor = 2;
        chanOut->neveEngine.setOversampleFactor(2);
    } else if (engineType == 3) {  // DM2plus engine - default 2x
        chanOut->dangerousOversampleFactor = 2;
        chanOut->dangerousEngine.setOversampleFactor(2);
    }

    // Reset Drive to 0% and Character to 50% when switching engines
    chanOut->params[ChanOut::DRIVE_PARAM].setValue(0.0f);
    chanOut->params[ChanOut::CHARACTER_PARAM].setValue(0.5f);
}

// TC Button Widgets (matching Shape/C1EQ/C1COMP implementation)
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

// C1 White Round Button with amber LED (matches other modules)
struct C1WhiteRoundButton : app::SvgSwitch {
    app::ModuleLightWidget* light;

    C1WhiteRoundButton() {
        momentary = false;
        latch = true;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/CustomButton_0.svg")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/CustomButton_1.svg")));
        light = new MediumSimpleLight<OrangeLight>;
        light->box.size = light->box.size.mult(0.75f);
        light->box.pos = box.size.div(2).minus(light->box.size.div(2));
        addChild(light);
    }

    app::ModuleLightWidget* getLight() { return light; }
};

struct ChanOutWidget : ModuleWidget {
    void initVuMeterLights() {
        float vuStartY = 107.0f;
        float vuLedSpacing = 5.4f;
        float vuLeftX = 82.0f;   // Positioned in right column area
        float vuRightX = 88.0f;

        for (int i = 0; i < 17; ++i) {
            float yPos = vuStartY + (16 - i) * vuLedSpacing;

            if (i < 11) {
                auto* leftLight = createLightCentered<TinyLight<GreenLight>>(
                    Vec(vuLeftX, yPos), module, ChanOut::VU_LIGHTS_LEFT + i);
                addChild(leftLight);
                auto* rightLight = createLightCentered<TinyLight<GreenLight>>(
                    Vec(vuRightX, yPos), module, ChanOut::VU_LIGHTS_RIGHT + i);
                addChild(rightLight);
            } else if (i < 14) {
                auto* leftLight = createLightCentered<TinyLight<YellowLight>>(
                    Vec(vuLeftX, yPos), module, ChanOut::VU_LIGHTS_LEFT + i);
                addChild(leftLight);
                auto* rightLight = createLightCentered<TinyLight<YellowLight>>(
                    Vec(vuRightX, yPos), module, ChanOut::VU_LIGHTS_RIGHT + i);
                addChild(rightLight);
            } else {
                auto* leftLight = createLightCentered<TinyLight<RedLight>>(
                    Vec(vuLeftX, yPos), module, ChanOut::VU_LIGHTS_LEFT + i);
                addChild(leftLight);
                auto* rightLight = createLightCentered<TinyLight<RedLight>>(
                    Vec(vuRightX, yPos), module, ChanOut::VU_LIGHTS_RIGHT + i);
                addChild(rightLight);
            }
        }
    }

    ChanOutWidget(ChanOut* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ChanOut.svg")));

        addChild(createWidget<ScrewBlack>(Vec(0, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // CHAN-OUT title text (TC style)
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
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "CHAN-OUT", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "CHAN-OUT", NULL);
            }
        };

        TitleLabel* titleLabel = new TitleLabel();
        titleLabel->box.pos = Vec(60, 10);  // X=60 (center of 8HP module), Y=10 (same as ORDER)
        titleLabel->box.size = Vec(104, 20);
        addChild(titleLabel);

        initVuMeterLights();

        // VU Meter scale labels (using VCV Rack system font)
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
        vuMinLabel->box.pos = Vec(93, 189);
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
        vu24Label->box.pos = Vec(93, 168);
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
        vu12Label->box.pos = Vec(93, 152);
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
        vu6Label->box.pos = Vec(93, 135);
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
        vu0Label->box.pos = Vec(93, 119);
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
        vuPlus6Label->box.pos = Vec(93, 103);
        vuPlus6Label->box.size = Vec(20, 8);
        addChild(vuPlus6Label);

        // Add goniometer display (between engine switches and LUFS meter with 2px clearance)
        GoniometerDisplay* goniometerDisplay = new GoniometerDisplay(module);
        goniometerDisplay->box.pos = Vec(16, 54);     // Moved up 3px (was 57)
        goniometerDisplay->box.size = Vec(88, 22.5f); // Increased by 3px from top
        addChild(goniometerDisplay);

        // Add LUFS meter at same position as ChanIn (Vec(16, 78.5f))
        LUFSMeterDisplay* lufsMeter = new LUFSMeterDisplay(module);
        lufsMeter->box.pos = Vec(16, 78.5f);
        lufsMeter->box.size = Vec(88, 7.5f);
        addChild(lufsMeter);

        // Connect LUFS meter to module (thread-safe atomic store)
        if (module) {
            module->lufsMeter.store(lufsMeter);
        }

        // Character Engine Switches (matching C1COMP position and style)
        CharacterEngineSwitchWidget* engineSwitches = new CharacterEngineSwitchWidget(
            module,
            module ? &module->characterEngine : nullptr
        );
        engineSwitches->box.pos = Vec(14, 43);
        engineSwitches->box.size = Vec(92, 12);
        addChild(engineSwitches);

        // Character Engine Label - displays current engine name
        struct CharacterEngineLabel : Widget {
            ChanOut* module;
            CharacterEngineLabel(ChanOut* m) : module(m) {}
            void draw(const DrawArgs& args) override {
                if (!module) return;

                const char* engineNames[4] = {"STANDARD", "2520", "8816", "DM2+"};
                int engine = module->characterEngine;
                engine = clamp(engine, 0, 3);

                nvgFontSize(args.vg, 6.0f);
                nvgFontFaceId(args.vg, APP->window->uiFont->handle);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 200));  // Amber
                nvgText(args.vg, 0, box.size.y / 2, engineNames[engine], NULL);
            }
        };

        CharacterEngineLabel* engineLabel = new CharacterEngineLabel(module);
        engineLabel->box.pos = Vec(46, 45);
        engineLabel->box.size = Vec(50, 6);
        addChild(engineLabel);

        // Display toggle switch (upper right corner, matching C1COMP style)
        struct DisplayToggleSwitch : widget::OpaqueWidget {
            ChanOut* module = nullptr;
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
                bool displayOn = module ? (module->params[ChanOut::DISPLAY_ENABLE_PARAM].getValue() > 0.5f) : true;

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
                    float currentValue = module->params[ChanOut::DISPLAY_ENABLE_PARAM].getValue();
                    module->params[ChanOut::DISPLAY_ENABLE_PARAM].setValue(currentValue > 0.5f ? 0.0f : 1.0f);

                    e.consume(this);
                }
            }
        };

        DisplayToggleSwitch* displayToggle = new DisplayToggleSwitch();
        displayToggle->module = module;
        displayToggle->box.pos = Vec(96, 43);  // Upper right corner of display area (same as C1COMP)
        displayToggle->box.size = Vec(12, 12);
        addChild(displayToggle);

        // Twisted Cable logo - using shared widget
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::FULL, module);
        tcLogo->box.pos = Vec(60, 355);
        addChild(tcLogo);

        // Control labels (matching CHAN-IN style)
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

        // Left column encoders with labels (matching CHAN-IN right column Y positions) - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(35, 125), module, ChanOut::DRIVE_PARAM));
        LedRingOverlay* driveRing = new LedRingOverlay(module, ChanOut::DRIVE_PARAM);
        driveRing->box.pos = Vec(35 - 25, 125 - 25);
        addChild(driveRing);
        ControlLabel* driveLabel = new ControlLabel("DRIVE");
        driveLabel->box.pos = Vec(35, 149);  // 24px clearance below encoder
        driveLabel->box.size = Vec(40, 10);
        addChild(driveLabel);

        addParam(createParamCentered<C1Knob280>(Vec(35, 175), module, ChanOut::CHARACTER_PARAM));
        LedRingOverlay* characterRing = new LedRingOverlay(module, ChanOut::CHARACTER_PARAM);
        characterRing->box.pos = Vec(35 - 25, 175 - 25);
        addChild(characterRing);
        ControlLabel* characterLabel = new ControlLabel("CHAR");
        characterLabel->box.pos = Vec(35, 199);  // 24px clearance below encoder
        characterLabel->box.size = Vec(40, 10);
        addChild(characterLabel);

        addParam(createParamCentered<C1Knob280>(Vec(35, 225), module, ChanOut::PAN_PARAM));
        LedRingOverlay* panRing = new LedRingOverlay(module, ChanOut::PAN_PARAM);
        panRing->box.pos = Vec(35 - 25, 225 - 25);
        addChild(panRing);
        ControlLabel* panLabel = new ControlLabel("PAN");
        panLabel->box.pos = Vec(35, 249);  // 24px clearance below encoder
        panLabel->box.size = Vec(40, 10);
        addChild(panLabel);

        // Right column encoder (matching CHAN-IN gain position) - using C1Knob280 with LED ring
        addParam(createParamCentered<C1Knob280>(Vec(85, 225), module, ChanOut::VOLUME_PARAM));

        // Add LED ring overlay centered on VOLUME knob
        LedRingOverlay* ledRing = new LedRingOverlay(module, ChanOut::VOLUME_PARAM);
        ledRing->box.pos = Vec(85 - 25, 225 - 25); // Center 50x50 box on knob center (85, 225)
        addChild(ledRing);

        // Dim and Mute buttons flanking the volume encoder
        C1WhiteRoundButton* dimButton = createParamCentered<C1WhiteRoundButton>(Vec(61, 210), module, ChanOut::DIM_BUTTON_PARAM);
        addParam(dimButton);
        dimButton->getLight()->module = module;
        if (module) {
            dimButton->getLight()->firstLightId = ChanOut::DIM_BUTTON_LIGHT;
        }
        ControlLabel* dimLabel = new ControlLabel("D");
        dimLabel->box.pos = Vec(61, 226);  // 16px clearance below button
        dimLabel->box.size = Vec(40, 10);
        addChild(dimLabel);

        C1WhiteRoundButton* muteButton = createParamCentered<C1WhiteRoundButton>(Vec(108, 210), module, ChanOut::MUTE_BUTTON_PARAM);
        addParam(muteButton);
        muteButton->getLight()->module = module;
        if (module) {
            muteButton->getLight()->firstLightId = ChanOut::MUTE_BUTTON_LIGHT;
        }
        ControlLabel* muteLabel = new ControlLabel("M");
        muteLabel->box.pos = Vec(108, 226);  // 16px clearance below button
        muteLabel->box.size = Vec(40, 10);
        addChild(muteLabel);

        ControlLabel* volumeLabel = new ControlLabel("VOLUME");
        volumeLabel->box.pos = Vec(85, 249);  // 24px clearance below encoder
        volumeLabel->box.size = Vec(40, 10);
        addChild(volumeLabel);

        // I/O jacks centered under encoder columns (X=35 for IN, X=85 for OUT)
        // Box: y=267, height=62, so jacks at y=267+16=283 and y=267+46=313 (with 4px top clearance + 12px to center + 30px spacing)
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(35, 283), module, ChanOut::LEFT_INPUT));
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(35, 313), module, ChanOut::RIGHT_INPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(85, 283), module, ChanOut::LEFT_OUTPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(85, 313), module, ChanOut::RIGHT_OUTPUT));

        // I/O Labels - exact same styling as other modules
        struct InLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

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
        inLabel->box.pos = Vec(35, 330);  // Centered at X=35 under IN box
        inLabel->box.size = Vec(20, 10);
        addChild(inLabel);

        struct OutLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

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
        outLabel->box.pos = Vec(85, 330);  // Centered at X=85 under OUT box
        outLabel->box.size = Vec(20, 10);
        addChild(outLabel);
    }

    // Trim gain slider quantity (for context menu)
    struct TrimGainQuantity : Quantity {
        float* trimGainSrc;

        TrimGainQuantity(float* trimGainSrc) {
            this->trimGainSrc = trimGainSrc;
        }

        void setValue(float value) override {
            float gainInDB = math::clamp(value, getMinValue(), getMaxValue());
            *trimGainSrc = std::pow(10.0f, gainInDB / 20.0f);
        }

        float getValue() override {
            return 20.0f * std::log10(*trimGainSrc);
        }

        float getMinValue() override { return -12.0f; }
        float getMaxValue() override { return 12.0f; }
        float getDefaultValue() override { return 0.0f; }
        float getDisplayValue() override { return getValue(); }

        std::string getDisplayValueString() override {
            float valGain = getDisplayValue();
            valGain = std::round(valGain * 10.0f) / 10.0f;  // Round to 0.1 dB
            return string::f("%.1f", math::normalizeZero(valGain));
        }

        void setDisplayValue(float displayValue) override { setValue(displayValue); }
        std::string getLabel() override { return "Trim"; }
        std::string getUnit() override { return " dB"; }
    };

    struct TrimGainSlider : ui::Slider {
        TrimGainSlider(float* trimGainSrc) {
            quantity = new TrimGainQuantity(trimGainSrc);
        }
        ~TrimGainSlider() {
            delete quantity;
        }
    };

    // Dim gain slider quantity (for context menu)
    struct DimGainQuantity : Quantity {
        float* dimGainSrc;
        float* dimGainIntegerDBSrc;
        ChanOut* module;

        DimGainQuantity(ChanOut* m, float* dimGainSrc, float* dimGainIntegerDBSrc) {
            this->module = m;
            this->dimGainSrc = dimGainSrc;
            this->dimGainIntegerDBSrc = dimGainIntegerDBSrc;
        }

        void setValue(float value) override {
            float gainInDB = math::clamp(value, getMinValue(), getMaxValue());
            float gainLin = std::pow(10.0f, gainInDB / 20.0f);
            *dimGainSrc = gainLin;
            *dimGainIntegerDBSrc = module->calcDimGainIntegerDB(gainLin);
        }

        float getValue() override {
            return 20.0f * std::log10(*dimGainSrc);
        }

        float getMinValue() override { return -30.0f; }
        float getMaxValue() override { return -1.0f; }
        float getDefaultValue() override { return -12.0f; }
        float getDisplayValue() override { return getValue(); }

        std::string getDisplayValueString() override {
            float valGain = getDisplayValue();
            valGain = std::round(valGain);
            return string::f("%g", math::normalizeZero(valGain));
        }

        void setDisplayValue(float displayValue) override { setValue(displayValue); }
        std::string getLabel() override { return "Dim gain"; }
        std::string getUnit() override { return " dB"; }
    };

    struct DimGainSlider : ui::Slider {
        DimGainSlider(ChanOut* module, float* dimGainSrc, float* dimGainIntegerDBSrc) {
            quantity = new DimGainQuantity(module, dimGainSrc, dimGainIntegerDBSrc);
        }
        ~DimGainSlider() {
            delete quantity;
        }
    };

    void appendContextMenu(Menu* menu) override {
        ChanOut* module = static_cast<ChanOut*>(this->module);
        if (!module)
            return;

        menu->addChild(new MenuSeparator());

        // Trim gain slider
        TrimGainSlider* trimSliderItem = new TrimGainSlider(&(module->trimGain));
        trimSliderItem->box.size.x = 200.0f;
        menu->addChild(trimSliderItem);

        // Dim gain slider
        DimGainSlider* dimSliderItem = new DimGainSlider(module, &(module->dimGain), &(module->dimGainIntegerDB));
        dimSliderItem->box.size.x = 200.0f;
        menu->addChild(dimSliderItem);

        menu->addChild(new MenuSeparator());

        // Character Engine submenu
        menu->addChild(createSubmenuItem("Character Engine", "",
            [=](Menu* menu) {
                // Standard engine
                menu->addChild(createCheckMenuItem("Standard", "",
                    [=]() { return module->characterEngine == 0; },
                    [=]() { module->characterEngine = 0; }
                ));

                // 2520 with oversampling submenu
                menu->addChild(createSubmenuItem("2520", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("8×", "",
                    [=]() { return module->characterEngine == 1 && module->oversampleFactor == 8; },
                    [=]() {
                        module->characterEngine = 1;
                        module->oversampleFactor = 8;
                        module->pending2520OversampleFactor.store(8);
                    }
                ));
                menu->addChild(createCheckMenuItem("4×", "",
                    [=]() { return module->characterEngine == 1 && module->oversampleFactor == 4; },
                    [=]() {
                        module->characterEngine = 1;
                        module->oversampleFactor = 4;
                        module->pending2520OversampleFactor.store(4);
                    }
                ));
                menu->addChild(createCheckMenuItem("2×", "",
                    [=]() { return module->characterEngine == 1 && module->oversampleFactor == 2; },
                    [=]() {
                        module->characterEngine = 1;
                        module->oversampleFactor = 2;
                        module->pending2520OversampleFactor.store(2);
                    }
                ));
                menu->addChild(createCheckMenuItem("OFF", "",
                    [=]() { return module->characterEngine == 1 && module->oversampleFactor == 1; },
                    [=]() {
                        module->characterEngine = 1;
                        module->oversampleFactor = 1;
                        module->pending2520OversampleFactor.store(1);
                    }
                ));
            }
        ));

        // 8816 with oversampling submenu
        menu->addChild(createSubmenuItem("8816", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("8×", "",
                    [=]() { return module->characterEngine == 2 && module->neveOversampleFactor == 8; },
                    [=]() {
                        module->characterEngine = 2;
                        module->neveOversampleFactor = 8;
                        module->pendingNeveOversampleFactor.store(8);
                    }
                ));
                menu->addChild(createCheckMenuItem("4×", "",
                    [=]() { return module->characterEngine == 2 && module->neveOversampleFactor == 4; },
                    [=]() {
                        module->characterEngine = 2;
                        module->neveOversampleFactor = 4;
                        module->pendingNeveOversampleFactor.store(4);
                    }
                ));
                menu->addChild(createCheckMenuItem("2×", "",
                    [=]() { return module->characterEngine == 2 && module->neveOversampleFactor == 2; },
                    [=]() {
                        module->characterEngine = 2;
                        module->neveOversampleFactor = 2;
                        module->pendingNeveOversampleFactor.store(2);
                    }
                ));
                menu->addChild(createCheckMenuItem("OFF", "",
                    [=]() { return module->characterEngine == 2 && module->neveOversampleFactor == 1; },
                    [=]() {
                        module->characterEngine = 2;
                        module->neveOversampleFactor = 1;
                        module->pendingNeveOversampleFactor.store(1);
                    }
                ));
            }
        ));
                menu->addChild(createSubmenuItem("DM2+", "",
                    [=](Menu* menu) {
                        menu->addChild(createCheckMenuItem("8×", "",
                            [=]() { return module->characterEngine == 3 && module->dangerousOversampleFactor == 8; },
                            [=]() {
                                module->characterEngine = 3;
                                module->dangerousOversampleFactor = 8;
                                module->pendingDangerousOversampleFactor.store(8);
                            }
                        ));
                        menu->addChild(createCheckMenuItem("4×", "",
                            [=]() { return module->characterEngine == 3 && module->dangerousOversampleFactor == 4; },
                            [=]() {
                                module->characterEngine = 3;
                                module->dangerousOversampleFactor = 4;
                                module->pendingDangerousOversampleFactor.store(4);
                            }
                        ));
                        menu->addChild(createCheckMenuItem("2×", "",
                            [=]() { return module->characterEngine == 3 && module->dangerousOversampleFactor == 2; },
                            [=]() {
                                module->characterEngine = 3;
                                module->dangerousOversampleFactor = 2;
                                module->pendingDangerousOversampleFactor.store(2);
                            }
                        ));
                        menu->addChild(createCheckMenuItem("OFF", "",
                            [=]() { return module->characterEngine == 3 && module->dangerousOversampleFactor == 1; },
                            [=]() {
                                module->characterEngine = 3;
                                module->dangerousOversampleFactor = 1;
                                module->pendingDangerousOversampleFactor.store(1);
                            }
                        ));
                    }
                ));
            }
        ));

        menu->addChild(new MenuSeparator());

        // Operating Mode submenu
        menu->addChild(createSubmenuItem("Operating Mode", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("Master Output", "",
                    [=]() { return module->outputMode == 0; },
                    [=]() { module->setOutputMode(0); }
                ));
                menu->addChild(createCheckMenuItem("Channel Output", "",
                    [=]() { return module->outputMode == 1; },
                    [=]() { module->setOutputMode(1); }
                ));
            }
        ));

        // Peak Hold submenu
        menu->addChild(createSubmenuItem("Peak Hold", "",
            [=](Menu* menu) {
                // ON checkbox at top
                menu->addChild(createBoolPtrMenuItem("ON", "", &module->peakHoldEnabled));

                // Hold Time submenu in middle
                menu->addChild(createSubmenuItem("Hold Time", string::f("%.1fs", module->peakHoldTime),
                    [=](Menu* menu) {
                        std::vector<float> times = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f};
                        std::vector<std::string> labels = {"0.5s", "1.0s", "1.5s", "2.0s", "3.0s", "5.0s"};

                        for (size_t i = 0; i < times.size(); i++) {
                            menu->addChild(createCheckMenuItem(labels[i], "",
                                [=]() { return module->peakHoldTime == times[i]; },
                                [=]() { module->peakHoldTime = times[i]; }
                            ));
                        }
                    }
                ));

                // Fall Rate submenu at bottom
                menu->addChild(createSubmenuItem("Fall Rate", string::f("%.0f dB/s", module->peakFallRate),
                    [=](Menu* menu) {
                        std::vector<float> rates = {12.0f, 24.0f, 48.0f, 96.0f};
                        std::vector<std::string> labels = {"12 dB/s (slow)", "24 dB/s (medium)", "48 dB/s (fast)", "96 dB/s (instant)"};

                        for (size_t i = 0; i < rates.size(); i++) {
                            menu->addChild(createCheckMenuItem(labels[i], "",
                                [=]() { return module->peakFallRate == rates[i]; },
                                [=]() { module->peakFallRate = rates[i]; }
                            ));
                        }
                    }
                ));
            }
        ));

        menu->addChild(new MenuSeparator());
    }
};

} // namespace

Model* modelChanOut = createModel<ChanOut, ChanOutWidget>("ChanOut");