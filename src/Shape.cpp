#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include <cmath>
#include <algorithm>
#include <atomic>
#include <thread>

// Custom ParamQuantity for Bypass button with ON/OFF labels
struct BypassParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "ON";
    }
};

// Custom ParamQuantity for Hard Gate button with ON/OFF labels
struct HardGateParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "ON";
    }
};

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

// Forward declarations
struct Shape;
struct GateWaveformWidget;

// Expander message struct for SHAPE-CV (SH-X) communication
struct ShapeExpanderMessage {
    float thresholdCV;    // -1.0 to +1.0 (attenuated, represents ±60dB range)
    float sustainCV;      // -1.0 to +1.0 (attenuated, 0-300ms range)
    float releaseCV;      // -1.0 to +1.0 (attenuated, 0.1s-4s range)
    float modeCV;         // Gate: >1V = hard gate mode
};

/*
   A noise gate with punch, sustain, and VU metering.

   CRITICAL: VCV Rack Sample Rate Handling Requirements
   ===================================================

   1. NEVER hardcode sample rates (44.1kHz, 48kHz, etc.) in calculations
   2. ALWAYS use APP->engine->getSampleRate() to get the actual engine sample rate
   3. Users can set ANY sample rate: 44.1kHz, 48kHz, 96kHz, 192kHz, or custom rates
   4. All timing calculations MUST be sample-rate independent
   5. Call prepare() or equivalent with actual sample rate in sampleRateChange()
   6. Default values should be reasonable fallbacks, but actual sample rate must override

   Examples:
   - Correct: float delayMs = 1000.0f * samples / APP->engine->getSampleRate()
   - Wrong:   float delayMs = 1000.0f * samples / 44100.0f

   - Correct: int delaySamples = (int)(delayMs * 0.001f * APP->engine->getSampleRate())
   - Wrong:   int delaySamples = (int)(delayMs * 0.001f * 44100.0f)
*/
class ShapeGateDSP {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        envelope = 0.0f;
        smoothedGain = 1.0f;
        punchEnvelope = 0.0f;
        lastGateState = false;
        updateCoefficients();
    }
    void setParameters(float thresholdDb,
                       float hardness,
                       float releaseMs,
                       float sustainMs,
                       float punchAmount,
                       float attackMs,
                       bool use10V = false,
                       int curveType = 0) {
        // Recalibrated threshold scaling for real VCV Rack signal levels
        // Real measurements: typical kick = ~5V, needs threshold around 3-4.5V range
        // Map dB range to practical VCV Rack voltages instead of traditional audio scaling
        float maxVoltage = use10V ? 10.0f : 5.0f;
        float practicalRange = maxVoltage * 0.8f; // Use 80% of max as practical range

        // Map -60dB to 0dB onto 0V to practicalRange (0V to 4V for 5V ref, 0V to 8V for 10V ref)
        float normalizedThreshold = (thresholdDb + 60.0f) / 60.0f; // -60dB to 0dB -> 0.0 to 1.0
        threshold = normalizedThreshold * practicalRange;
        hardGate = hardness > 0.5f;

        // Store parameters for coefficient calculation
        releaseTimeMs = releaseMs;
        sustainTimeMs = sustainMs;
        this->punchAmount = punchAmount;
        this->attackTimeMs = attackMs;
        this->curveType = curveType;

        updateCoefficients();
    }
    float processSample(float x) {
        // Envelope follower with separate attack/release
        float rectified = std::fabs(x);
        if (rectified > envelope) {
            envelope = rectified + (envelope - rectified) * attackCoeff; // Fast attack
        } else {
            envelope = rectified + (envelope - rectified) * releaseCoeff; // User release
        }

        // Gate with sustain/hold logic
        bool gateOpen = envelope >= threshold;
        float targetGain = 1.0f;

        if (gateOpen) {
            holdCounter = holdSamples; // Reset hold time when signal above threshold
            targetGain = 1.0f;
        } else {
            // Signal below threshold
            if (holdCounter > 0) {
                --holdCounter;
                targetGain = 1.0f; // Hold at full gain during sustain period
            } else {
                // Apply gating after sustain period
                if (hardGate) {
                    targetGain = 0.0f; // Hard gate: complete silence
                } else {
                    // Soft gate: gradual reduction based on ratio
                    float ratio = envelope / threshold;
                    targetGain = ratio * ratio; // Squared curve for smooth transition
                }
            }
        }

        // Detect gate opening transition (closed → open)
        bool gateOpening = gateOpen && !lastGateState;
        lastGateState = gateOpen;

        // Trigger punch envelope on gate opening
        if (gateOpening && punchAmount > 0.0f) {
            punchEnvelope = punchAmount; // Start at punch amount (0.0 to 1.0)
        }

        // Decay punch envelope with fast release (10-20ms)
        if (punchEnvelope > 0.0f) {
            punchEnvelope *= punchDecayCoeff; // Fast exponential decay
        }

        // Apply attack/release smoothing to base gain
        if (targetGain > smoothedGain) {
            smoothedGain = targetGain + (smoothedGain - targetGain) * attackCoeff;
        } else {
            smoothedGain = targetGain + (smoothedGain - targetGain) * releaseCoeff;
        }

        // Add punch envelope as transient boost on top of smoothed gain
        float finalGain = smoothedGain * (1.0f + punchEnvelope);

        float output = x * finalGain;
        meterEnv = 0.99f * meterEnv + 0.01f * std::fabs(output);
        return output;
    }
    float getMeterDb() const {
        return 20.0f * std::log10(meterEnv + 1e-12f);
    }

    // Get gate attenuation in dB (0dB = gate open, negative = gate closing)
    float getGateAttenuation() const {
        float attenDb = 20.0f * std::log10(smoothedGain + 1e-12f);
        return attenDb;  // Returns 0dB to -60dB (or lower)
    }

    // NEW: Process with external sidechain key signal
    float processSampleWithKey(float audioIn, float keySignal) {
        // Use external key for envelope detection instead of audio input
        float rectified = std::fabs(keySignal);
        if (rectified > envelope) {
            envelope = rectified + (envelope - rectified) * attackCoeff; // Fast attack
        } else {
            envelope = rectified + (envelope - rectified) * releaseCoeff; // User release
        }

        // Gate with sustain/hold logic (identical to processSample)
        bool gateOpen = envelope >= threshold;
        float targetGain = 1.0f;

        if (gateOpen) {
            holdCounter = holdSamples;
            targetGain = 1.0f;
        } else {
            if (holdCounter > 0) {
                --holdCounter;
                targetGain = 1.0f;
            } else {
                if (hardGate) {
                    targetGain = 0.0f;
                } else {
                    float ratio = envelope / threshold;
                    targetGain = ratio * ratio;
                }
            }
        }

        // Detect gate opening transition (closed → open)
        bool gateOpening = gateOpen && !lastGateState;
        lastGateState = gateOpen;

        // Trigger punch envelope on gate opening
        if (gateOpening && punchAmount > 0.0f) {
            punchEnvelope = punchAmount; // Start at punch amount (0.0 to 1.0)
        }

        // Decay punch envelope with fast release (10-20ms)
        if (punchEnvelope > 0.0f) {
            punchEnvelope *= punchDecayCoeff; // Fast exponential decay
        }

        // Apply attack/release smoothing to base gain
        if (targetGain > smoothedGain) {
            smoothedGain = targetGain + (smoothedGain - targetGain) * attackCoeff;
        } else {
            smoothedGain = targetGain + (smoothedGain - targetGain) * releaseCoeff;
        }

        // Add punch envelope as transient boost on top of smoothed gain
        float finalGain = smoothedGain * (1.0f + punchEnvelope);

        // Apply gain to audio input (not key signal)
        float output = audioIn * finalGain;
        meterEnv = 0.99f * meterEnv + 0.01f * std::fabs(output);
        return output;
    }

private:
    void updateCoefficients() {
        attackCoeff = std::exp(-2.2f / (attackTimeMs * sr / 1000.0f));

        switch (curveType) {
            case 0:
                releaseCoeff = std::exp(-2.2f / (releaseTimeMs * sr / 1000.0f));
                break;
            case 1:
                releaseCoeff = std::exp(-4.6f / (releaseTimeMs * sr / 1000.0f));
                break;
            case 2:
                releaseCoeff = std::exp(-1.1f / (releaseTimeMs * sr / 1000.0f));
                break;
            case 3:
                releaseCoeff = std::exp(-1.5f / (releaseTimeMs * sr / 1000.0f));
                break;
            case 4:
                releaseCoeff = std::exp(-5.0f / (releaseTimeMs * sr / 1000.0f));
                break;
            case 5:
                releaseCoeff = std::exp(-1.0f / (releaseTimeMs * sr / 1000.0f));
                break;
            default:
                releaseCoeff = std::exp(-2.2f / (releaseTimeMs * sr / 1000.0f));
                break;
        }

        holdSamples = static_cast<int>(0.001f * sustainTimeMs * sr);

        // Punch envelope decay: 15ms decay time for transient boost
        float punchDecayTimeMs = 15.0f;
        punchDecayCoeff = std::exp(-2.2f / (punchDecayTimeMs * sr / 1000.0f));
    }
    double sr = 44100.0;
    float envelope = 0.0f;
    float smoothedGain = 1.0f;
    float threshold = 0.01f;
    bool hardGate = false;
    float attackCoeff = 0.999f;
    float releaseCoeff = 0.999f;
    int holdSamples = 0;
    int holdCounter = 0;
    float punchAmount = 0.0f;
    float meterEnv = 0.0f;

    float releaseTimeMs = 1000.0f;
    float sustainTimeMs = 500.0f;
    float attackTimeMs = 0.1f;  // User-controllable attack time (0.1ms to 25ms)
    int curveType = 0;

    // Punch transient boost envelope
    float punchEnvelope = 0.0f;
    float punchDecayCoeff = 0.999f;
    bool lastGateState = false;
};

// Gate waveform display widget (adapted from 4ms WavePlayer + AudioDisplay time segments)
struct GateWaveformWidget : OpaqueWidget {
    Shape* _module;

    // Time window configuration (from AudioDisplay)
    struct TimeWindow {
        int bufferSize;
        int decimation;
    };

    static constexpr TimeWindow timeWindows[4] = {
        {1024, 5},    // Beat: 100ms - High resolution, transient analysis
        {2048, 23},   // Envelope: 1s - Medium resolution, ADSR visualization
        {2048, 47},   // Rhythm: 2s - Medium resolution, beat patterns
        {4096, 47}    // Measure: 4s - Lower resolution, musical phrases
    };

    // Dynamic sample storage with maximum buffer size
    static constexpr int MAX_BUFFER_SIZE = 4096;
    std::vector<std::pair<float, float>> samples;
    std::atomic<int> newest_sample{0};
    int currentBufferSize = 2048;      // Default to "Envelope" mode buffer size
    int currentTimeWindow = 1;         // Default to "Envelope" mode (index 1)
    int sampleCounter = 0;

    // Oversampling for proper peak detection
    float oversample_min = 1.0f;
    float oversample_max = -1.0f;
    float decimation_counter = 0.0f;
    int currentDecimation = 23;        // Default to "Envelope" mode decimation

    // Visual styling - unified amber theme matching LED rings
    uint8_t wave_r = 0xFF, wave_g = 0xC0, wave_b = 0x50;  // Amber
    uint8_t bg_r = 0, bg_g = 0, bg_b = 0;                  // Black background

    // Fade-out animation when signal stops
    float fadeOpacity = 1.0f;
    double lastSignalTime = 0.0;
    static constexpr float FADE_DURATION = 0.3f;  // 300ms fade-out

    // Display enable state (updated from module process() function)
    std::atomic<bool> displayEnabled{true};

    // Smooth scrolling animation state (per-instance)
    double lastDrawTime = 0.0;
    float scrollOffset = 0.0f;

    GateWaveformWidget(Shape* module) : _module(module) {
        // Initialize with maximum buffer size to handle all time windows
        samples.resize(MAX_BUFFER_SIZE);

        // Initialize with silence
        for (auto& sample : samples) {
            sample = {0.0f, 0.0f};
        }

        // Set default time window parameters
        setTimeWindow(currentTimeWindow);
    }

    void setTimeWindow(int windowIndex) {
        if (windowIndex < 0 || windowIndex >= 4) return;

        currentTimeWindow = windowIndex;
        const TimeWindow& tw = timeWindows[windowIndex];
        currentBufferSize = tw.bufferSize;
        currentDecimation = tw.decimation;

        // Reset sample counter when changing time windows
        sampleCounter = 0;
    }

    void addSample(float sample) {
        if (!_module) return;

        // Clamp sample to ±1.0 range for VCV Rack ±5V -> ±1.0 normalized
        sample = rack::math::clamp(sample / 5.0f, -1.0f, 1.0f);

        // Track min/max for oversampling
        oversample_min = std::min(oversample_min, sample);
        oversample_max = std::max(oversample_max, sample);

        // Use time-window specific decimation
        sampleCounter++;
        if (sampleCounter % currentDecimation == 0) {
            // Add min/max pair to circular buffer
            int t = newest_sample.load();
            if (++t >= currentBufferSize) {
                t = 0;
            }

            samples[t] = {oversample_min, oversample_max};
            newest_sample.store(t);

            // Reset for next accumulation
            oversample_min = 1.0f;
            oversample_max = -1.0f;
        }
    }

    void draw(const DrawArgs& args) override {
        if (!_module) return;

        // Check if display is enabled via toggle switch
        bool isDisplayEnabled = displayEnabled.load();

        if (isDisplayEnabled) {
            // Display ON: always show at full opacity, no fade-out
            fadeOpacity = 1.0f;
        } else {
            // Display OFF: check buffer for signal and fade out gracefully
            bool hasSignal = false;
            int checkSamples = currentBufferSize / 10;  // Only check most recent 10% of buffer
            int startCheck = newest_sample.load();
            for (int i = 0; i < checkSamples; i++) {
                int idx = (startCheck - i + currentBufferSize) % currentBufferSize;
                if (std::abs(samples[idx].first) > 0.0001f || std::abs(samples[idx].second) > 0.0001f) {
                    hasSignal = true;
                    break;
                }
            }

            // Update fade opacity based on signal presence (only when display is OFF)
            double currentTime = glfwGetTime();
            if (hasSignal) {
                // Signal still in buffer - instant full opacity and reset timer
                fadeOpacity = 1.0f;
                lastSignalTime = currentTime;
            } else {
                // No signal in buffer - calculate fade-out
                double timeSinceSignal = currentTime - lastSignalTime;
                if (timeSinceSignal < FADE_DURATION) {
                    // Fading out
                    fadeOpacity = 1.0f - (timeSinceSignal / FADE_DURATION);
                } else {
                    // Fully faded out
                    fadeOpacity = 0.0f;
                }
            }
        }

        // Don't draw if fully faded out
        if (fadeOpacity <= 0.0f) return;

        // Calculate waveform drawing area
        float wave_height = box.size.y * 0.45f;  // 90% of half-height to stay within bounds
        float center_y = box.size.y * 0.5f;

        // Draw waveform using 4ms approach: filled polygon with min/max envelope
        nvgBeginPath(args.vg);

        // Start with oldest sample (use current buffer size)
        int start = newest_sample.load() + 1;
        if (start >= currentBufferSize) start = 0;

        // Smooth scrolling with sub-pixel interpolation
        // Calculate fractional position for smoother animation
        float timeNow = glfwGetTime();
        double deltaTime = timeNow - lastDrawTime;
        lastDrawTime = timeNow;

        // Smooth scroll offset (0.0 to 1.0) for sub-sample interpolation
        float scrollSpeed = currentDecimation * 60.0f / APP->engine->getSampleRate(); // Pixels per second
        scrollOffset += scrollSpeed * deltaTime;
        if (scrollOffset >= 1.0f) scrollOffset -= 1.0f;

        // Draw top contour (min values) left to right with interpolation
        bool first_point = true;
        int i = start;
        for (int x = 0; x < currentBufferSize; x++) {
            // Sub-pixel positioning for smoother scroll
            float x_pos = ((float)x - scrollOffset) * box.size.x / (float)currentBufferSize;
            x_pos = clamp(x_pos, 0.0f, box.size.x);  // Clamp to display bounds
            float y_pos = center_y - (samples[i].first * wave_height);  // Min value (top)

            if (first_point) {
                nvgMoveTo(args.vg, x_pos, y_pos);
                first_point = false;
            } else {
                nvgLineTo(args.vg, x_pos, y_pos);
            }

            i = (i + 1) % currentBufferSize;
        }

        // Continue shape, drawing bottom contour (max values) right to left
        for (int x = currentBufferSize - 1; x >= 0; x--) {
            if (--i < 0) i = currentBufferSize - 1;

            float x_pos = ((float)x - scrollOffset) * box.size.x / (float)currentBufferSize;
            x_pos = clamp(x_pos, 0.0f, box.size.x);  // Clamp to display bounds
            float y_pos = center_y - (samples[i].second * wave_height);  // Max value (bottom)

            nvgLineTo(args.vg, x_pos, y_pos);
        }

        nvgClosePath(args.vg);

        // Fill with semi-transparent amber (50% opacity * fade)
        nvgFillColor(args.vg, nvgRGBA(wave_r, wave_g, wave_b, (uint8_t)(0x7F * fadeOpacity)));
        nvgFill(args.vg);

        // Stroke with solid amber (with fade)
        nvgStrokeColor(args.vg, nvgRGBA(wave_r, wave_g, wave_b, (uint8_t)(0xFF * fadeOpacity)));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);
    }

};

// External definition for static constexpr member (required for ODR-use)
constexpr GateWaveformWidget::TimeWindow GateWaveformWidget::timeWindows[4];

struct Shape : Module {
    enum ParamIds {
        BYPASS_PARAM,
        THRESHOLD_PARAM,
        HARD_GATE_PARAM,
        RELEASE_PARAM,
        SUSTAIN_PARAM,
        PUNCH_PARAM,
        DISPLAY_ENABLE_PARAM,  // Enable/disable display visibility
        PARAMS_LEN
    };

    enum InputIds {
        LEFT_INPUT,
        RIGHT_INPUT,
        SIDECHAIN_INPUT,
        INPUTS_LEN
    };

    enum OutputIds {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightIds {
        BYPASS_LIGHT,
        HARD_GATE_LIGHT,
        VU_LIGHT_0,
        VU_LIGHT_1,
        VU_LIGHT_2,
        VU_LIGHT_3,
        VU_LIGHT_4,
        VU_LIGHT_5,
        VU_LIGHT_6,
        VU_LIGHT_7,
        VU_LIGHT_8,
        VU_LIGHT_9,
        VU_LIGHT_10,
        LIGHTS_LEN
    };

    ShapeGateDSP leftGate;
    ShapeGateDSP rightGate;

    dsp::ClockDivider lightDivider;  // LED update clock divider (update every 256 samples)

    // Gate waveform display widget (atomic for thread safety)
    std::atomic<GateWaveformWidget*> gateWaveform{nullptr};
    int savedTimeWindow = 1;  // Temporarily store loaded time window state (default: Envelope mode)

    // Expander message buffers for SHAPE-CV (SH-X) communication
    ShapeExpanderMessage rightMessages[2] = {};  // Double buffer for thread safety

    float smoothedThreshold = -20.0f;
    float smoothedHardness = 0.0f;
    float smoothedRelease = 1.0f;
    float smoothedSustain = 0.5f;
    float smoothedPunch = 0.5f;

    float inputRMS = 0.0f;
    float outputRMS = 0.0f;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    float dynamicDifference = 0.0f;
    float processingIntensityHistory[128] = {0.0f};
    int historyIndex = 0;

    // VU meter: gate attenuation tracking
    float peakGateAttenuation = 0.0f;
    float peakDecayCoeff = 0.995f;  // Exponential decay coefficient
    bool vuMeterBarMode = false;    // false = dot mode, true = bar mode

    bool bypassed = false;
    bool use10VReference = false;
    float attackTimeMs = 0.1f;  // Attack time (0.1ms = auto/fixed, up to 25ms)

    enum ReleaseCurve {
        CURVE_LINEAR = 0,
        CURVE_EXPONENTIAL,
        CURVE_LOGARITHMIC,
        CURVE_SSL,
        CURVE_DBX,
        CURVE_DRAWMER,
        CURVE_COUNT
    };
    ReleaseCurve releaseCurve = CURVE_LINEAR;

    // Thread safety for shutdown
    std::atomic<bool> isShuttingDown{false};

    Shape() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam<BypassParamQuantity>(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass");
        configParam(THRESHOLD_PARAM, -60.f, 0.f, -60.f, "Gate Threshold", " dB");
        configParam<HardGateParamQuantity>(HARD_GATE_PARAM, 0.f, 1.f, 0.f, "Hard Gate");
        configParam(RELEASE_PARAM, 0.1f, 4.f, 0.1f, "Gate Release", " s");
        configParam(SUSTAIN_PARAM, 0.f, 300.f, 0.f, "Sustain", " ms");
        configParam(PUNCH_PARAM, 0.f, 1.f, 0.0f, "Punch", "%", 0.f, 100.f);
        configParam(DISPLAY_ENABLE_PARAM, 0.0f, 1.0f, 1.0f, "Display Visibility");  // Default ON

        configInput(LEFT_INPUT, "Left");
        configInput(RIGHT_INPUT, "Right");
        configInput(SIDECHAIN_INPUT, "Sidechain");
        configOutput(LEFT_OUTPUT, "Left");
        configOutput(RIGHT_OUTPUT, "Right");

        // VCV Rack engine-level bypass (right-click menu)
        configBypass(LEFT_INPUT, LEFT_OUTPUT);
        configBypass(RIGHT_INPUT, RIGHT_OUTPUT);

        lightDivider.setDivision(256);  // Update LEDs every 256 samples (187.5Hz at 48kHz)

        leftGate.prepare(APP->engine->getSampleRate());
        rightGate.prepare(APP->engine->getSampleRate());
    }

    ~Shape() {
        // Safe destructor - prevent crashes on sudden exit
        isShuttingDown.store(true);
        gateWaveform.store(nullptr);

        // Brief pause to let audio thread see the shutdown flag
        std::this_thread::sleep_for(std::chrono::microseconds(100));

        // No complex operations, just ensure safe state
    }

    void onRandomize(const RandomizeEvent& e) override {
        (void)e;  // Suppress unused parameter warning
        // Disable randomize - do nothing
    }

    void onReset() override {
        // Reset all parameters to defaults
        Module::onReset();

        // Reset context menu settings to defaults
        use10VReference = false;         // 5V Reference
        releaseCurve = CURVE_LINEAR;     // Linear
        attackTimeMs = 0.1f;             // Auto (0.1ms)
    }

    void onAdd() override {
        // Module added to rack - no special action needed
    }

    void onRemove() override {
        // Module removed from rack - no special action needed
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        leftGate.prepare(sr);
        rightGate.prepare(sr);
    }

    void process(const ProcessArgs& args) override {
        // Thread-safe shutdown check - prevent crashes on sudden exit
        if (isShuttingDown.load()) {
            outputs[LEFT_OUTPUT].setVoltage(0.0f);
            outputs[RIGHT_OUTPUT].setVoltage(0.0f);
            return;
        }

        // Get inputs
        float leftIn = inputs[LEFT_INPUT].getVoltage();
        float rightIn = inputs[RIGHT_INPUT].isConnected() ? inputs[RIGHT_INPUT].getVoltage() : leftIn;

        bypassed = params[BYPASS_PARAM].getValue() > 0.5f;

        // Update LEDs at reduced rate (every 256 samples = 187.5Hz at 48kHz)
        bool updateLights = lightDivider.process();
        if (updateLights) {
            lights[BYPASS_LIGHT].setBrightness(params[BYPASS_PARAM].getValue() * 0.65f);
        }

        if (bypassed) {
            outputs[LEFT_OUTPUT].setVoltage(leftIn);
            outputs[RIGHT_OUTPUT].setVoltage(rightIn);

            // Update VU lights at reduced rate when bypassed
            if (updateLights) {
                for (int i = 0; i < 11; i++) {
                    lights[VU_LIGHT_0 + i].setBrightness(i == 0 ? 1.0f : 0.0f);
                }
            }
            return;
        }

        // Read CV modulation from SH-X expander (if connected)
        float thresholdCVMod = 0.0f;
        float sustainCVMod = 0.0f;
        float releaseCVMod = 0.0f;
        bool modeCV = false;

        if (rightExpander.module && rightExpander.module->model == modelShapeCV) {
            // Read from SH-X's consumer message (where SH-X wrote the data)
            ShapeExpanderMessage* msg = (ShapeExpanderMessage*)(rightExpander.module->leftExpander.consumerMessage);
            thresholdCVMod = msg->thresholdCV * 60.0f;  // ±60dB range
            sustainCVMod = msg->sustainCV * 300.0f;     // 0-300ms range
            releaseCVMod = msg->releaseCV * 3.9f;       // 0.1s-4s range (3.9 = 4.0 - 0.1)
            modeCV = (msg->modeCV > 1.0f);
        }

        // Apply CV modulation to parameters
        float thresholdParam = clamp(params[THRESHOLD_PARAM].getValue() + thresholdCVMod, -60.0f, 0.0f);
        float sustainParam = clamp(params[SUSTAIN_PARAM].getValue() + sustainCVMod, 0.0f, 300.0f);
        float releaseParam = clamp(params[RELEASE_PARAM].getValue() + releaseCVMod, 0.1f, 4.0f);
        bool hardGateMode = (params[HARD_GATE_PARAM].getValue() > 0.5f) || modeCV;

        // Update Hard Gate LED at reduced rate
        if (updateLights) {
            lights[HARD_GATE_LIGHT].setBrightness(hardGateMode ? 0.65f : 0.0f);
        }

        const float smoothingRate = 0.01f;
        smoothedThreshold += (thresholdParam - smoothedThreshold) * smoothingRate;
        smoothedHardness = hardGateMode ? 1.0f : 0.0f;
        smoothedRelease += (releaseParam - smoothedRelease) * smoothingRate;
        smoothedSustain += (sustainParam - smoothedSustain) * smoothingRate;
        smoothedPunch += (params[PUNCH_PARAM].getValue() - smoothedPunch) * smoothingRate;

        leftGate.setParameters(smoothedThreshold, smoothedHardness,
                              smoothedRelease * 1000.0f, smoothedSustain, smoothedPunch, attackTimeMs, use10VReference, releaseCurve);
        rightGate.setParameters(smoothedThreshold, smoothedHardness,
                               smoothedRelease * 1000.0f, smoothedSustain, smoothedPunch, attackTimeMs, use10VReference, releaseCurve);

        // Sidechain processing: use external key if connected with active channels, otherwise use main input
        float leftOut, rightOut;
        if (inputs[SIDECHAIN_INPUT].getChannels() > 0) {
            // External sidechain mode: use sidechain signal for both L/R gate detection
            float scSignal = inputs[SIDECHAIN_INPUT].getVoltage();
            leftOut = leftGate.processSampleWithKey(leftIn, scSignal);
            rightOut = rightGate.processSampleWithKey(rightIn, scSignal);
        } else {
            // Normal mode: each gate uses its own input for detection
            leftOut = leftGate.processSample(leftIn);
            rightOut = rightGate.processSample(rightIn);
        }

        // VU meter: direct gate attenuation measurement (like C1-COMP)
        float gateAttenuation = leftGate.getGateAttenuation();  // Get from left channel DSP

        // Instant attack, exponential decay (like C1-COMP GR meter)
        if (gateAttenuation < peakGateAttenuation) {
            peakGateAttenuation = gateAttenuation;  // Instant (more closing)
        } else {
            peakGateAttenuation *= peakDecayCoeff;  // Smooth decay (gate opening)
        }

        // Map 0dB to -60dB across 11 LEDs, LEFT to RIGHT
        // LED 0 (left) = -60dB (gate fully closed)
        // LED 10 (right) = 0dB (gate fully open)
        float attenAbs = clamp(-peakGateAttenuation, 0.0f, 60.0f);  // Convert to positive range
        int ledIndex = 10 - (int)(attenAbs / 6.0f);  // 6dB per LED, inverted for left→right
        ledIndex = clamp(ledIndex, 0, 10);

        // Update VU meter LEDs at reduced rate
        if (updateLights) {
            if (vuMeterBarMode) {
                // Bar mode: all LEDs from left up to current level light up
                for (int i = 0; i < 11; i++) {
                    lights[VU_LIGHT_0 + i].setBrightness(i <= ledIndex ? 1.0f : 0.0f);
                }
            } else {
                // Dot mode: only current LED
                for (int i = 0; i < 11; i++) {
                    lights[VU_LIGHT_0 + i].setBrightness(i == ledIndex ? 1.0f : 0.0f);
                }
            }
        }

        outputs[LEFT_OUTPUT].setVoltage(leftOut);
        outputs[RIGHT_OUTPUT].setVoltage(rightOut);

        // Feed samples to gate waveform display and update display enable state
        auto* waveform = gateWaveform.load();
        if (waveform) {
            bool displayOn = params[DISPLAY_ENABLE_PARAM].getValue() > 0.5f;
            waveform->displayEnabled.store(displayOn);

            if (displayOn) {
                // Feed real gate output signal
                float monoOut = (leftOut + rightOut) * 0.5f;
                waveform->addSample(monoOut);
            } else {
                // Feed silence to allow natural decay of waveform display
                waveform->addSample(0.0f);
            }
        }
    }

    bool isBypassed() const { return bypassed; }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "bypassed", json_boolean(bypassed));
        json_object_set_new(rootJ, "use10VReference", json_boolean(use10VReference));
        json_object_set_new(rootJ, "releaseCurve", json_integer(releaseCurve));
        json_object_set_new(rootJ, "attackTimeMs", json_real(attackTimeMs));
        json_object_set_new(rootJ, "vuMeterBarMode", json_boolean(vuMeterBarMode));
        auto* waveform = gateWaveform.load();
        if (waveform) {
            json_object_set_new(rootJ, "currentTimeWindow", json_integer(waveform->currentTimeWindow));
        }
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* bypassedJ = json_object_get(rootJ, "bypassed");
        if (bypassedJ) {
            bypassed = json_boolean_value(bypassedJ);
            params[BYPASS_PARAM].setValue(bypassed ? 1.0f : 0.0f);
        }

        json_t* use10VJ = json_object_get(rootJ, "use10VReference");
        if (use10VJ) {
            use10VReference = json_boolean_value(use10VJ);
        }

        json_t* releaseCurveJ = json_object_get(rootJ, "releaseCurve");
        if (releaseCurveJ) {
            int curve = json_integer_value(releaseCurveJ);
            if (curve >= 0 && curve < CURVE_COUNT) {
                releaseCurve = (ReleaseCurve)curve;
            }
        }

        json_t* attackTimeMsJ = json_object_get(rootJ, "attackTimeMs");
        if (attackTimeMsJ) {
            attackTimeMs = json_real_value(attackTimeMsJ);
            // Clamp to valid range
            attackTimeMs = clamp(attackTimeMs, 0.1f, 25.0f);
        }

        json_t* vuMeterBarModeJ = json_object_get(rootJ, "vuMeterBarMode");
        if (vuMeterBarModeJ) {
            vuMeterBarMode = json_boolean_value(vuMeterBarModeJ);
        }

        json_t* timeWindowJ = json_object_get(rootJ, "currentTimeWindow");
        if (timeWindowJ) {
            savedTimeWindow = json_integer_value(timeWindowJ);
            // Apply to widget if it exists (may not exist yet during load)
            auto* waveform = gateWaveform.load();
            if (waveform) {
                waveform->setTimeWindow(savedTimeWindow);
            }
        }
    }
};

// Independent time segment switch widget (positioned on panel, not in waveform widget)
struct TimeSegmentSwitch : widget::OpaqueWidget {
    GateWaveformWidget* waveform = nullptr;

    static constexpr float SWITCH_SIZE = 5.6f;  // 5.6px squares (matches ChanIn)
    static constexpr float SWITCH_SPACING = 7.0f;  // 7px spacing (matches ChanIn)
    static constexpr uint8_t wave_r = 0xD9;
    static constexpr uint8_t wave_g = 0x8E;
    static constexpr uint8_t wave_b = 0x48;

    void draw(const DrawArgs& args) override {
        if (!waveform) return;

        // Draw 4 rectangles
        for (int i = 0; i < 4; i++) {
            float x = (i * SWITCH_SPACING);
            float y = 0.0f;

            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, SWITCH_SIZE, SWITCH_SIZE, 1.0f);

            // Only draw outline, no fill
            nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 0xFF));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            // Draw amber checkmark for active switch
            if (i == waveform->currentTimeWindow) {
                nvgStrokeColor(args.vg, nvgRGBA(wave_r, wave_g, wave_b, 0xFF));
                nvgStrokeWidth(args.vg, 1.2f);
                nvgLineCap(args.vg, NVG_ROUND);

                // Draw checkmark: short line + longer line
                float centerX = x + SWITCH_SIZE * 0.5f;
                float centerY = y + SWITCH_SIZE * 0.5f;
                float size = SWITCH_SIZE * 0.3f;

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
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && waveform) {
            // Check if click is on any time segment switch
            for (int i = 0; i < 4; i++) {
                float x = (i * SWITCH_SPACING);
                float y = 0.0f;

                if (e.pos.x >= x && e.pos.x <= x + SWITCH_SIZE &&
                    e.pos.y >= y && e.pos.y <= y + SWITCH_SIZE) {

                    waveform->setTimeWindow(i);
                    e.consume(this);
                    return;
                }
            }
        }
        OpaqueWidget::onButton(e);
    }
};

struct ShapeWidget : ModuleWidget {
    // Store waveform widget reference
    GateWaveformWidget* gateWaveform = nullptr;

    ShapeWidget(Shape* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Shape.svg")));


        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // SHAPE title text in title section
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
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "SHAPE", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "SHAPE", NULL);
            }
        };

        TitleLabel* titleLabel = new TitleLabel();
        titleLabel->box.pos = Vec(60, 10);
        titleLabel->box.size = Vec(104, 20);
        addChild(titleLabel);

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

        C1WhiteRoundButton* bypassButton = createParamCentered<C1WhiteRoundButton>(Vec(23, 26), module, Shape::BYPASS_PARAM);
        bypassButton->getLight()->module = module;
        if (module) {
            bypassButton->getLight()->firstLightId = Shape::BYPASS_LIGHT;
        }
        addParam(bypassButton);


        // VU Meter Labels using NanoVG (since SVG text doesn't render)
        struct VULabel : Widget {
            std::string text;
            VULabel(std::string t) : text(t) {}
            void draw(const DrawArgs& args) override {
                nvgFontSize(args.vg, 6.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 180));
                nvgText(args.vg, box.size.x/2, box.size.y/2, text.c_str(), NULL);
            }
        };

        VULabel* minLabel = new VULabel("-");
        minLabel->box.pos = Vec(17, 83);
        minLabel->box.size = Vec(6, 6);
        addChild(minLabel);

        VULabel* maxLabel = new VULabel("+");
        maxLabel->box.pos = Vec(97, 83);
        maxLabel->box.size = Vec(6, 6);
        addChild(maxLabel);

        // Left column: encoder + button - C1Knob280 with LED ring
        addParam(createParamCentered<C1Knob280>(Vec(35, 145), module, Shape::THRESHOLD_PARAM));
        LedRingOverlay* thresholdRing = new LedRingOverlay(module, Shape::THRESHOLD_PARAM);
        thresholdRing->box.pos = Vec(35 - 25, 145 - 25);
        addChild(thresholdRing);

        // GATE label under the gate encoder with custom drawing for outline
        struct GateLabel : Widget {
            void draw(const DrawArgs& args) override {
                // Load and use custom Sono Medium font
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "GATE", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "GATE", NULL);
            }
        };

        GateLabel* gateLabel = new GateLabel();
        gateLabel->box.pos = Vec(25.5, 165);
        gateLabel->box.size = Vec(40, 10);         addChild(gateLabel);

        // Hard gate button - TC theme button
        C1WhiteRoundButton* hardGateButton = createParamCentered<C1WhiteRoundButton>(Vec(35, 195), module, Shape::HARD_GATE_PARAM);
        hardGateButton->getLight()->module = module;
        if (module) {
            hardGateButton->getLight()->firstLightId = Shape::HARD_GATE_LIGHT;
        }
        addParam(hardGateButton);

        // HARD GATE label under the button with custom drawing for outline - two lines
        struct HardGateLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

                // Ultra thin black outline for "HARD"
                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "HARD", NULL);
                            nvgText(args.vg, dx * 0.5f, (dy * 0.5f) + 10, "GATE", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "HARD", NULL);
                nvgText(args.vg, 0, 10, "GATE", NULL);
            }
        };

        HardGateLabel* hardGateLabel = new HardGateLabel();
        hardGateLabel->box.pos = Vec(35, 210);
        hardGateLabel->box.size = Vec(40, 20);         addChild(hardGateLabel);

        // Right column: 3 encoders - C1Knob280 with LED rings
        addParam(createParamCentered<C1Knob280>(Vec(85, 125), module, Shape::RELEASE_PARAM));
        LedRingOverlay* releaseRing = new LedRingOverlay(module, Shape::RELEASE_PARAM);
        releaseRing->box.pos = Vec(85 - 25, 125 - 25);
        addChild(releaseRing);

        // RELEASE label under the release encoder with custom drawing for outline
        struct ReleaseLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "RELEASE", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "RELEASE", NULL);
            }
        };

        ReleaseLabel* releaseLabel = new ReleaseLabel();
        releaseLabel->box.pos = Vec(70, 145);
        releaseLabel->box.size = Vec(40, 10);         addChild(releaseLabel);

        addParam(createParamCentered<C1Knob280>(Vec(85, 175), module, Shape::SUSTAIN_PARAM));
        LedRingOverlay* sustainRing = new LedRingOverlay(module, Shape::SUSTAIN_PARAM);
        sustainRing->box.pos = Vec(85 - 25, 175 - 25);
        addChild(sustainRing);

        // SUSTAIN label under the sustain encoder with custom drawing for outline
        struct SustainLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "SUSTAIN", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "SUSTAIN", NULL);
            }
        };

        SustainLabel* sustainLabel = new SustainLabel();
        sustainLabel->box.pos = Vec(70, 195);
        sustainLabel->box.size = Vec(40, 10);         addChild(sustainLabel);

        addParam(createParamCentered<C1Knob280>(Vec(85, 225), module, Shape::PUNCH_PARAM));
        LedRingOverlay* punchRing = new LedRingOverlay(module, Shape::PUNCH_PARAM);
        punchRing->box.pos = Vec(85 - 25, 225 - 25);
        addChild(punchRing);

        // PUNCH label under the punch encoder with custom drawing for outline
        struct PunchLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "PUNCH", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "PUNCH", NULL);
            }
        };

        PunchLabel* punchLabel = new PunchLabel();
        punchLabel->box.pos = Vec(72, 245);
        punchLabel->box.size = Vec(40, 10);         addChild(punchLabel);

        addInput(createInputCentered<ThemedPJ301MPort>(Vec(30, 284), module, Shape::LEFT_INPUT));
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(30, 314), module, Shape::RIGHT_INPUT));

        addInput(createInputCentered<ThemedPJ301MPort>(Vec(60, 299), module, Shape::SIDECHAIN_INPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(90, 284), module, Shape::LEFT_OUTPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(90, 314), module, Shape::RIGHT_OUTPUT));

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
        inLabel->box.pos = Vec(30, 330);
        inLabel->box.size = Vec(20, 10);         addChild(inLabel);

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
        outLabel->box.pos = Vec(90, 330);
        outLabel->box.size = Vec(20, 10);         addChild(outLabel);

        struct ScLabel : Widget {
            void draw(const DrawArgs& args) override {
                std::shared_ptr<Font> sonoFont = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));
                nvgFontFaceId(args.vg, sonoFont->handle);
                nvgFontSize(args.vg, 10.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

                nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00)); // Black outline
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx != 0 || dy != 0) {
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "SC", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "SC", NULL);
            }
        };

        ScLabel* scLabel = new ScLabel();
        scLabel->box.pos = Vec(60, 330);
        scLabel->box.size = Vec(20, 10);         addChild(scLabel);


        // Gate waveform display
        this->gateWaveform = new GateWaveformWidget(module);
        this->gateWaveform->box.pos = Vec(17, 54.6);  // 1px inset from left
        this->gateWaveform->box.size = Vec(86, 24.4);  // 2px narrower (1px each side)
        addChild(static_cast<Widget*>(this->gateWaveform));

        // Connect waveform widget to module for sample feeding
        if (module) {
            Shape* shapeModule = static_cast<Shape*>(module);
            shapeModule->gateWaveform.store(this->gateWaveform);
            // Apply saved time window state
            this->gateWaveform->setTimeWindow(shapeModule->savedTimeWindow);
        }

        // Time segment switches - independent widget positioned on panel
        TimeSegmentSwitch* timeSwitch = new TimeSegmentSwitch();
        timeSwitch->waveform = this->gateWaveform;
        timeSwitch->box.pos = Vec(16, 45);  // Original absolute position (was 14+2, 43+2)
        timeSwitch->box.size = Vec(26.6f, 5.6f);  // Width: 3*7 + 5.6 = 26.6, Height: 5.6
        addChild(timeSwitch);

        // Time segment labels - displays segment names
        struct TimeSegmentLabel : Widget {
            GateWaveformWidget* waveform;
            TimeSegmentLabel(GateWaveformWidget* w) : waveform(w) {}
            void draw(const DrawArgs& args) override {
                if (!waveform) return;

                const char* segmentNames[4] = {"BEAT", "ENV", "BAR", "PHRASE"};
                int current = waveform->currentTimeWindow;
                current = clamp(current, 0, 3);

                nvgFontSize(args.vg, 6.0f);
                nvgFontFaceId(args.vg, APP->window->uiFont->handle);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 200));  // Amber
                nvgText(args.vg, 0, box.size.y / 2, segmentNames[current], NULL);
            }
        };

        TimeSegmentLabel* segmentLabel = new TimeSegmentLabel(this->gateWaveform);
        segmentLabel->box.pos = Vec(46, 45);  // Right of switches, 1px above
        segmentLabel->box.size = Vec(50, 6);
        addChild(segmentLabel);

        // Add display toggle switch in upper right corner
        struct SimpleSwitch : widget::OpaqueWidget {
            Shape* module = nullptr;
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
                bool displayOn = module ? (module->params[Shape::DISPLAY_ENABLE_PARAM].getValue() > 0.5f) : true;

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
                    float currentValue = module->params[Shape::DISPLAY_ENABLE_PARAM].getValue();
                    module->params[Shape::DISPLAY_ENABLE_PARAM].setValue(currentValue > 0.5f ? 0.0f : 1.0f);

                    e.consume(this);
                }
            }
        };

        SimpleSwitch* simpleSwitch = new SimpleSwitch();
        simpleSwitch->module = module;
        simpleSwitch->box.pos = Vec(96, 43); // Upper right corner of display area
        simpleSwitch->box.size = Vec(9.6f, 9.6f); // Size for click area
        addChild(simpleSwitch);


        addChild(createLightCentered<TinyLight<RedLight>>(Vec(20, 91), module, Shape::VU_LIGHT_0));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(28, 91), module, Shape::VU_LIGHT_1));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(36, 91), module, Shape::VU_LIGHT_2));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(44, 91), module, Shape::VU_LIGHT_3));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(52, 91), module, Shape::VU_LIGHT_4));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(60, 91), module, Shape::VU_LIGHT_5));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(68, 91), module, Shape::VU_LIGHT_6));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(76, 91), module, Shape::VU_LIGHT_7));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(84, 91), module, Shape::VU_LIGHT_8));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(92, 91), module, Shape::VU_LIGHT_9));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(100, 91), module, Shape::VU_LIGHT_10));

        // Twisted Cable logo - using shared widget
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::FULL, module);
        tcLogo->box.pos = Vec(60, 355);
        addChild(tcLogo);
    }

    void appendContextMenu(Menu* menu) override {
        Shape* module = getModule<Shape>();
        if (!module)
            return;

        menu->addChild(new MenuSeparator);

        // Attack time slider - forward declare inner struct first
        struct AttackTimeQuantity : Quantity {
            Shape* module;
            AttackTimeQuantity(Shape* m) : module(m) {}

            void setValue(float value) override {
                if (module) {
                    module->attackTimeMs = clamp(value, 0.1f, 25.0f);
                }
            }

            float getValue() override {
                return module ? module->attackTimeMs : 0.1f;
            }

            float getMinValue() override { return 0.1f; }
            float getMaxValue() override { return 25.0f; }
            float getDefaultValue() override { return 0.1f; }

            std::string getLabel() override {
                return "Attack";
            }

            std::string getUnit() override {
                return " ms";
            }

            std::string getDisplayValueString() override {
                float val = getValue();
                if (val <= 0.11f) {
                    return "Auto (0.1ms)";
                }
                return string::f("%.1f", val);
            }

            int getDisplayPrecision() override {
                return 1;
            }
        };

        struct AttackTimeSlider : ui::Slider {
            Shape* module;
            AttackTimeSlider(Shape* m) : module(m) {
                quantity = new AttackTimeQuantity(m);
            }
            ~AttackTimeSlider() {
                delete quantity;
            }
        };

        AttackTimeSlider* attackSlider = new AttackTimeSlider(module);
        attackSlider->box.size.x = 200.0f;
        menu->addChild(attackSlider);

        menu->addChild(new MenuSeparator);

        // Release Curves submenu
        menu->addChild(createSubmenuItem("Release Curves", "",
            [=](Menu* menu) {
                const char* curveNames[] = {"Linear (Default)", "Exponential", "Logarithmic", "SSL G-Series", "DBX 160X", "Drawmer DS201"};

                for (int i = 0; i < Shape::CURVE_COUNT; i++) {
                    menu->addChild(createCheckMenuItem(curveNames[i], "",
                        [=]() { return module->releaseCurve == i; },
                        [=]() { module->releaseCurve = (Shape::ReleaseCurve)i; }
                    ));
                }
            }
        ));

        menu->addChild(new MenuSeparator);

        // Threshold Reference submenu
        menu->addChild(createSubmenuItem("Threshold Reference", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("5V Reference (Subtle)", "",
                    [=]() { return !module->use10VReference; },
                    [=]() { module->use10VReference = false; }
                ));
                menu->addChild(createCheckMenuItem("10V Reference (Prominent)", "",
                    [=]() { return module->use10VReference; },
                    [=]() { module->use10VReference = true; }
                ));
            }
        ));

        menu->addChild(new MenuSeparator);

        // VU Meter Mode submenu
        menu->addChild(createSubmenuItem("VU Meter Mode", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("Dot", "",
                    [=]() { return !module->vuMeterBarMode; },
                    [=]() { module->vuMeterBarMode = false; }
                ));
                menu->addChild(createCheckMenuItem("Bar", "",
                    [=]() { return module->vuMeterBarMode; },
                    [=]() { module->vuMeterBarMode = true; }
                ));
            }
        ));

    }
};

} // namespace

Model* modelShape = createModel<Shape, ShapeWidget>("Shape");