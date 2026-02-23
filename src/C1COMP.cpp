#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include "../shared/include/CompressorEngine.hpp"
#include "../shared/include/VCACompressor.hpp"
#include "../shared/include/FETCompressor.hpp"
#include "../shared/include/OpticalCompressor.hpp"
#include "../shared/include/VariMuCompressor.hpp"

// Custom ParamQuantity for Bypass button with ON/OFF labels
struct BypassParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return (value == 0) ? "OFF" : "ON";
    }
};

namespace {

// Expander message struct for C1COMPCV (COM-X) communication
struct C1COMPExpanderMessage {
    float ratioCV;     // -1.0 to +1.0 (attenuated)
    float thresholdCV; // -1.0 to +1.0 (attenuated)
    float releaseCV;   // -1.0 to +1.0 (attenuated)
    float mixCV;       // -1.0 to +1.0 (attenuated)
};

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

// C1 Snap Knob with 280° rotation - for discrete parameter selection
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

// Attack LED Ring - Snaps to 6 discrete positions (SSL G attack times)
struct AttackLedRing : widget::TransparentWidget {
    Module* module;
    int paramId;

    AttackLedRing(Module* m, int pid) : module(m), paramId(pid) {
        box.size = Vec(50, 50);
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Get parameter value (0.0 to 5.0)
        ParamQuantity* pq = module->paramQuantities[paramId];
        float paramValue = pq ? pq->getValue() : 0.0f;

        // Get discrete position (0-5, already snapped by VCV Rack)
        int attackIndex = (int)std::round(paramValue);
        attackIndex = clamp(attackIndex, 0, 5);

        // Convert to LED position (6 positions → map to 15 LEDs accounting for 80° bottom gap)
        // Positions: 0, 3, 6, 9, 11, 14
        const int ledPositions[6] = {0, 3, 6, 9, 11, 14};
        int activeLED = ledPositions[attackIndex];

        // LED ring specifications
        const int dotCount = 15;
        const float gapDeg = 80.0f;
        const float gap = gapDeg * (M_PI / 180.0f);
        const float start = -M_PI * 1.5f + gap * 0.5f;
        const float end   =  M_PI * 0.5f  - gap * 0.5f;
        const float totalSpan = end - start;

        // Ring geometry
        const float knobRadius = 24.095f / 2.0f;
        const float ringOffset = 3.5f;
        const float ringR = knobRadius + ringOffset;
        const float ledR = 0.9f;

        const float cx = box.size.x / 2.0f;
        const float cy = box.size.y / 2.0f;

        // Draw only the 6 discrete LED positions (remove intermediate LEDs)
        const int visibleLEDs[6] = {0, 3, 6, 9, 11, 14};
        const char* attackLabels[6] = {"0.1", "0.3", "1.0", "3.0", "10", "30"};

        for (int i = 0; i < 6; ++i) {
            int ledIndex = visibleLEDs[i];
            float t = (dotCount == 1) ? 0.0f : (float)ledIndex / (float)(dotCount - 1);
            float angle = start + t * totalSpan;

            // Fine-tune LED positions
            if (ledIndex == 6) {
                angle -= 9.0f * (M_PI / 180.0f);  // LED 6: -9° counter-clockwise
            } else if (ledIndex == 9) {
                angle -= 15.0f * (M_PI / 180.0f);  // LED 9: -15° counter-clockwise (5° + 10° additional)
            }

            float px = cx + ringR * std::cos(angle);
            float py = cy + ringR * std::sin(angle);

            // Only the active position is bright, all others dim
            int alpha = (ledIndex == activeLED) ? 230 : 71;

            // Draw LED dot
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, px, py, ledR);
            nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, alpha));
            nvgFill(args.vg);

            // Draw attack time label next to LED (further out on the arc)
            float labelRadius = ringR + 6.0f;  // 6px outside the LED ring
            float labelX = cx + labelRadius * std::cos(angle);
            float labelY = cy + labelRadius * std::sin(angle);

            // Fine-tune label position: align "3.0" with "1.0" vertically
            if (i == 3) {  // Position 3 is "3.0"
                labelY += 0.5f;  // Nudge down slightly
            }

            nvgFontSize(args.vg, 4.0f);
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0xFF, 0xFF, 0xFF, 200));
            nvgText(args.vg, labelX, labelY, attackLabels[i], NULL);
        }
    }
};

// Release LED Ring - Smooth tracking 0-90%, alternating animation 90-100% (AUTO mode)
struct ReleaseLedRing : widget::TransparentWidget {
    Module* module;
    int paramId;

    ReleaseLedRing(Module* m, int pid) : module(m), paramId(pid) {
        box.size = Vec(50, 50);
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        // Get parameter value (0.0 to 1.0)
        ParamQuantity* pq = module->paramQuantities[paramId];
        float paramValue = pq ? pq->getScaledValue() : 0.0f;

        // LED ring specifications
        const int dotCount = 15;
        const float gapDeg = 80.0f;
        const float gap = gapDeg * (M_PI / 180.0f);
        const float start = -M_PI * 1.5f + gap * 0.5f;
        const float end   =  M_PI * 0.5f  - gap * 0.5f;
        const float totalSpan = end - start;

        // Ring geometry
        const float knobRadius = 24.095f / 2.0f;
        const float ringOffset = 3.5f;
        const float ringR = knobRadius + ringOffset;
        const float ledR = 0.9f;

        const float cx = box.size.x / 2.0f;
        const float cy = box.size.y / 2.0f;

        // Check if in AUTO mode (90-100% range)
        bool autoMode = (paramValue >= 0.9f);

        if (autoMode) {
            // AUTO mode: Burst flashing with silent gaps (matching hardware behavior)
            // Cycle: 0.0-0.8s = flashing (4 alternations @ 2.5Hz), 0.8-1.3s = silent (0.5 second gap)
            float time = APP->window->getFrameTime();
            float cyclePhase = std::fmod(time, 1.3f);  // 1.3 second total cycle

            // Draw all LEDs
            for (int i = 0; i < dotCount; ++i) {
                float t = (dotCount == 1) ? 0.0f : (float)i / (float)(dotCount - 1);
                float angle = start + t * totalSpan;
                float px = cx + ringR * std::cos(angle);
                float py = cy + ringR * std::sin(angle);

                int alpha = 71;  // Default dim

                if (cyclePhase < 0.8f) {
                    // Flashing period: alternate LEDs 13-14 at 2.5 Hz
                    float flashPhase = std::fmod(cyclePhase * 2.5f, 1.0f);  // 2.5 Hz within flash period
                    bool led13Active = (flashPhase < 0.5f);  // First half: LED 13

                    if (i == 13 && led13Active) {
                        alpha = 230;  // LED 13 bright during first half
                    } else if (i == 14 && !led13Active) {
                        alpha = 230;  // LED 14 bright during second half
                    }
                }
                // Silent gap (0.8-1.3s): all LEDs stay dim (alpha = 71)

                nvgBeginPath(args.vg);
                nvgCircle(args.vg, px, py, ledR);
                nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, alpha));
                nvgFill(args.vg);
            }
        } else {
            // Normal mode (0-90%): Smooth tracking with crossfade
            // Map 0.0-0.9 to LED positions 0-13 (leaving 14 for AUTO zone entry)
            float normalizedValue = paramValue / 0.9f;  // Rescale to 0.0-1.0
            float exactPos = normalizedValue * 13.0f;    // 0.0 to 13.0
            int led1 = (int)exactPos;
            int led2 = led1 + 1;
            float frac = exactPos - led1;

            led1 = clamp(led1, 0, 13);
            led2 = clamp(led2, 0, 13);

            // Draw all LEDs with smooth crossfade
            for (int i = 0; i < dotCount; ++i) {
                float t = (dotCount == 1) ? 0.0f : (float)i / (float)(dotCount - 1);
                float angle = start + t * totalSpan;
                float px = cx + ringR * std::cos(angle);
                float py = cy + ringR * std::sin(angle);

                int alpha = 71;  // Dim
                if (i == led1 && led1 != led2) {
                    alpha = 71 + (int)((230 - 71) * (1.0f - frac));
                } else if (i == led2 && led1 != led2) {
                    alpha = 71 + (int)((230 - 71) * frac);
                } else if (i == led1 && led1 == led2) {
                    alpha = 230;
                }

                nvgBeginPath(args.vg);
                nvgCircle(args.vg, px, py, ledR);
                nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, alpha));
                nvgFill(args.vg);
            }
        }
    }
};

// Forward declaration
struct C1COMP;

// Compressor Type Switch Widget - 4 rectangle switches for engine selection (matching ChanIn style)
struct CompressorTypeSwitchWidget : widget::TransparentWidget {
    Module* module;
    int* currentCompressorType = nullptr;

    static constexpr float SWITCH_SIZE = 5.6f;
    static constexpr float SWITCH_SPACING = 7.0f;

    CompressorTypeSwitchWidget(Module* m, int* typePtr)
        : module(m), currentCompressorType(typePtr) {}

    void draw(const DrawArgs& args) override {
        float switchSizePx = SWITCH_SIZE;
        float spacingPx = SWITCH_SPACING;

        // Draw 4 rectangles for VCA, FET, Optical, Vari-Mu
        for (int i = 0; i < 4; i++) {
            float x = 2.0f + (i * spacingPx);
            float y = 2.0f;

            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, switchSizePx, switchSizePx, 1.0f);

            nvgStrokeColor(args.vg, nvgRGBA(100, 100, 100, 255));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            // Draw amber checkmark for active compressor type
            if (currentCompressorType && i == *currentCompressorType) {
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

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && currentCompressorType) {
            float switchSizePx = SWITCH_SIZE;
            float spacingPx = SWITCH_SPACING;

            for (int i = 0; i < 4; i++) {
                float x = 2.0f + (i * spacingPx);
                float y = 2.0f;

                if (e.pos.x >= x && e.pos.x <= x + switchSizePx &&
                    e.pos.y >= y && e.pos.y <= y + switchSizePx) {
                    *currentCompressorType = i;
                    e.consume(this);
                    return;
                }
            }
        }
        TransparentWidget::onButton(e);
    }
};

// Peak Meter Display Widget - Horizontal bar meters (matching ChanIn style)
struct PeakMeterDisplay : widget::TransparentWidget {
    Module* module;
    bool isStereo;
    bool isInverted;  // For GR meter (right to left)
    float* peakLeft = nullptr;
    float* peakRight = nullptr;  // nullptr for mono GR meter

    float display_width = 88.0f;
    float display_height = 7.5f;

    uint8_t meter_r = 0xFF, meter_g = 0xC0, meter_b = 0x50;  // Amber
    uint8_t bg_r = 40, bg_g = 40, bg_b = 40;

    // Peak hold state
    float peakHoldLeft = 0.0f;
    float peakHoldRight = 0.0f;
    float peakHoldTimeLeft = 0.0f;
    float peakHoldTimeRight = 0.0f;
    float peakHoldTime = 0.5f;  // Peak hold time in seconds (default 0.5s)
    const char* meterLabel = nullptr;

    PeakMeterDisplay(Module* m, bool stereo, bool inverted, float* left, float* right = nullptr, float holdTime = 0.5f, const char* label = nullptr)
        : module(m), isStereo(stereo), isInverted(inverted), peakLeft(left), peakRight(right), peakHoldTime(holdTime), meterLabel(label) {}

    void draw(const DrawArgs& args) override {
        if (!module || !peakLeft) return;

        // Background bar
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

        if (isStereo && peakRight) {
            // Stereo: draw two bars with separator
            float bar_height = (display_height - 1.0f) * 0.5f;

            // Left channel (top half)
            float left_width = (display_width - 2.0f) * (*peakLeft);
            if (left_width > 1.0f) {
                NVGpaint leftGradient = nvgLinearGradient(args.vg,
                    1.0f, 0.0f,
                    1.0f + left_width, 0.0f,
                    nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),
                    nvgRGBA(meter_r, meter_g, meter_b, 200));

                nvgFillPaint(args.vg, leftGradient);
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 1.0f, 0.5f, left_width, bar_height);
                nvgFill(args.vg);
            }

            // Right channel (bottom half)
            float right_width = (display_width - 2.0f) * (*peakRight);
            if (right_width > 1.0f) {
                NVGpaint rightGradient = nvgLinearGradient(args.vg,
                    1.0f, 0.0f,
                    1.0f + right_width, 0.0f,
                    nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),
                    nvgRGBA(meter_r, meter_g, meter_b, 200));

                nvgFillPaint(args.vg, rightGradient);
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 1.0f, 0.5f + bar_height + 0.5f, right_width, bar_height);
                nvgFill(args.vg);
            }

            // Separator line between channels
            nvgStrokeColor(args.vg, nvgRGBA(bg_r, bg_g, bg_b, 255));
            nvgStrokeWidth(args.vg, 1.0f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 1.0f, display_height * 0.5f);
            nvgLineTo(args.vg, display_width - 1.0f, display_height * 0.5f);
            nvgStroke(args.vg);

        } else {
            // Mono: single bar (GR meter)
            if (isInverted) {
                // GR meter: inverted gradient (right to left)
                float bar_width = (display_width - 2.0f) * (*peakLeft);
                if (bar_width > 1.0f) {
                    float bar_x = display_width - 1.0f - bar_width;
                    NVGpaint grGradient = nvgLinearGradient(args.vg,
                        display_width - 1.0f, 0.0f,
                        bar_x, 0.0f,
                        nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),
                        nvgRGBA(meter_r, meter_g, meter_b, 200));

                    nvgFillPaint(args.vg, grGradient);
                    nvgBeginPath(args.vg);
                    nvgRect(args.vg, bar_x, 0.5f, bar_width, display_height - 1.0f);
                    nvgFill(args.vg);
                }
            } else {
                // Normal left-to-right gradient
                float bar_width = (display_width - 2.0f) * (*peakLeft);
                if (bar_width > 1.0f) {
                    NVGpaint gradient = nvgLinearGradient(args.vg,
                        1.0f, 0.0f,
                        1.0f + bar_width, 0.0f,
                        nvgRGBA(meter_r * 0.3f, meter_g * 0.3f, meter_b * 0.3f, 200),
                        nvgRGBA(meter_r, meter_g, meter_b, 200));

                    nvgFillPaint(args.vg, gradient);
                    nvgBeginPath(args.vg);
                    nvgRect(args.vg, 1.0f, 0.5f, bar_width, display_height - 1.0f);
                    nvgFill(args.vg);
                }
            }
        }

        // Update peak hold
        float deltaTime = APP->window->getLastFrameDuration();
        updatePeakHold(deltaTime);

        // Draw 0dB reference line (only for stereo IN/OUT meters, not GR meter)
        if (isStereo && !isInverted) {
            float zeroDbNorm = 60.0f / 66.0f;  // 0dB at 60/66 normalized position
            float zeroDbX = 1.0f + (display_width - 2.0f) * zeroDbNorm;

            nvgStrokeColor(args.vg, nvgRGBA(128, 128, 128, 150));  // Grey, semi-transparent
            nvgStrokeWidth(args.vg, 0.5f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, zeroDbX, 0.5f);
            nvgLineTo(args.vg, zeroDbX, display_height - 0.5f);
            nvgStroke(args.vg);

            // Draw peak hold dB readout at 0dB marker
            float maxPeakHold = std::max(peakHoldLeft, peakHoldRight);

            nvgFontSize(args.vg, 5.0f);
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 200));  // Amber matching meter color

            // Check if signal is present
            bool hasSignal = (maxPeakHold >= 0.0001f);

            if (hasSignal) {
                float peakDb = (maxPeakHold * 66.0f) - 60.0f;  // Convert 0.0-1.0 to -60dB to +6dB
                char peakText[16];
                snprintf(peakText, sizeof(peakText), "%.1f", peakDb);
                nvgText(args.vg, zeroDbX, display_height * 0.5f, peakText, NULL);
            } else {
                // Display infinity symbol when no signal
                nvgText(args.vg, zeroDbX, display_height * 0.5f, "\u221E", NULL);
            }
        }

        // Draw meter label inside the bar (matching LUFS meter style from ChanOut)
        if (meterLabel) {
            nvgFontSize(args.vg, 6.0f);
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgFillColor(args.vg, nvgRGBA(meter_r, meter_g, meter_b, 200));  // Amber

            // GR label on right side, others on left
            if (strcmp(meterLabel, "GR") == 0) {
                nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgText(args.vg, display_width - 2.0f, display_height * 0.5f, meterLabel, NULL);
            } else {
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgText(args.vg, 2.0f, display_height * 0.5f, meterLabel, NULL);
            }
        }

        // Draw peak hold indicators (white lines)
        drawPeakHoldIndicators(args);
    }

    void updatePeakHold(float deltaTime) {
        if (!peakLeft) return;

        // Update left channel peak hold
        if (*peakLeft > peakHoldLeft) {
            peakHoldLeft = *peakLeft;
            peakHoldTimeLeft = peakHoldTime;
        } else {
            peakHoldTimeLeft -= deltaTime;
            if (peakHoldTimeLeft <= 0.0f) {
                peakHoldLeft *= 0.95f;  // Decay
                if (peakHoldLeft < 0.01f) peakHoldLeft = 0.0f;
            }
        }

        // Update right channel peak hold (if stereo)
        if (isStereo && peakRight) {
            if (*peakRight > peakHoldRight) {
                peakHoldRight = *peakRight;
                peakHoldTimeRight = peakHoldTime;
            } else {
                peakHoldTimeRight -= deltaTime;
                if (peakHoldTimeRight <= 0.0f) {
                    peakHoldRight *= 0.95f;  // Decay
                    if (peakHoldRight < 0.01f) peakHoldRight = 0.0f;
                }
            }
        }
    }

    void drawPeakHoldIndicators(const DrawArgs& args) {
        nvgStrokeColor(args.vg, nvgRGBA(0xFF, 0xFF, 0xFF, 180));  // White
        nvgStrokeWidth(args.vg, 1.0f);
        nvgBeginPath(args.vg);

        if (isStereo && peakRight) {
            // Stereo: draw two peak hold lines (avoid 1.0px separator)
            // Batch both lines into single stroke for CHAN-IN-style blending effect

            // Left channel peak hold (top half) - stop before separator
            if (peakHoldLeft > 0.01f) {
                float x = 1.0f + (display_width - 2.0f) * peakHoldLeft;
                float separator_y = display_height * 0.5f;  // 3.75px
                nvgMoveTo(args.vg, x, 0.5f);
                nvgLineTo(args.vg, x, separator_y - 0.5f);  // Stop 0.5px before separator center
            }

            // Right channel peak hold (bottom half) - start after separator
            if (peakHoldRight > 0.01f) {
                float x = 1.0f + (display_width - 2.0f) * peakHoldRight;
                float separator_y = display_height * 0.5f;  // 3.75px
                nvgMoveTo(args.vg, x, separator_y + 0.5f);  // Start 0.5px after separator center (4.25px)
                nvgLineTo(args.vg, x, display_height);  // End at display bottom (7.5px)
            }
        } else {
            // Mono: single peak hold line (GR meter)
            if (peakHoldLeft > 0.01f) {
                float x;
                if (isInverted) {
                    // GR meter: peak hold from right (inverted)
                    x = display_width - 1.0f - (display_width - 2.0f) * peakHoldLeft;
                } else {
                    // Normal: peak hold from left
                    x = 1.0f + (display_width - 2.0f) * peakHoldLeft;
                }
                nvgMoveTo(args.vg, x, 0.5f);
                nvgLineTo(args.vg, x, display_height - 0.5f);
            }
        }

        nvgStroke(args.vg);
    }
};

// SSL G discrete attack times (6 positions)
static constexpr float attackValues[6] = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};

// C1COMP Module - SSL G-Style Glue Compressor
struct C1COMP : Module {
    enum ParamIds {
        BYPASS_PARAM,
        ATTACK_PARAM,
        RELEASE_PARAM,
        THRESHOLD_PARAM,
        RATIO_PARAM,
        DRY_WET_PARAM,
        DISPLAY_ENABLE_PARAM,
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

    // DSP core - pointer to current compressor engine
    CompressorEngine* comp = nullptr;

    dsp::ClockDivider lightDivider;  // LED update clock divider (update every 256 samples)

    // Compressor type selection
    enum CompressorType {
        VCA_TYPE = 0,
        FET_TYPE = 1,
        OPTICAL_TYPE = 2,
        VARIMU_TYPE = 3
    };
    int compressorType = VCA_TYPE;  // Default: VCA (SSL G)
    int lastCompressorType = VCA_TYPE;  // Track changes to trigger engine switch

    // Context menu settings
    bool vuMeterBarMode = false;  // false = dot mode, true = bar mode
    bool autoMakeup = false;
    bool use10VReference = false;
    float inputGainDb = 0.0f;   // -24dB to +24dB
    float outputGainDb = 0.0f;  // -24dB to +24dB
    float kneeOverride = -1.0f;  // -1 = Auto (use engine defaults), 0-12 = override knee width


    // Peak metering state (normalized 0.0-1.0 for display)
    float peakInputLeft = 0.0f;
    float peakInputRight = 0.0f;
    float peakGR = 0.0f;
    float peakOutputLeft = 0.0f;
    float peakOutputRight = 0.0f;

    // Peak decay coefficient (300ms decay time constant)
    float peakDecayCoeff = 0.0f;

    C1COMP() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Parameter configuration with proper ranges
        configParam<BypassParamQuantity>(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass");

        // Attack parameter with custom display (shows actual SSL G attack times)
        struct AttackParamQuantity : ParamQuantity {
            std::string getDisplayValueString() override {
                int index = (int)std::round(getValue());
                index = clamp(index, 0, 5);
                const char* attackLabels[6] = {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"};
                return attackLabels[index];
            }
        };
        configParam<AttackParamQuantity>(ATTACK_PARAM, 0.f, 5.f, 0.f, "Attack");  // 6 discrete positions (0-5), default: 0.1ms (index 0)
        getParamQuantity(ATTACK_PARAM)->snapEnabled = true;  // Snap to 6 discrete positions

        // Release parameter with custom display (shows "AUTO" in 90-100% range)
        struct ReleaseParamQuantity : ParamQuantity {
            std::string getDisplayValueString() override {
                float v = getValue();
                if (v >= 0.9f) {
                    return "AUTO";
                } else {
                    // Map 0-0.9 to 100ms-1200ms logarithmically
                    float normalizedRelease = v / 0.9f;
                    float releaseMs = 100.0f * std::pow(12.0f, normalizedRelease);
                    return string::f("%.0f ms", releaseMs);
                }
            }
        };
        configParam<ReleaseParamQuantity>(RELEASE_PARAM, 0.f, 1.f, 0.f, "Release");  // 100ms to 1200ms or AUTO, default: 100ms

        configParam(THRESHOLD_PARAM, 0.f, 1.f, 0.667f, "Threshold", " dB", 0.f, 30.f, -20.f);  // -20dB to +10dB, default: 0dB

        // Ratio parameter with custom display
        struct RatioParamQuantity : ParamQuantity {
            std::string getDisplayValueString() override {
                float v = getValue();
                float ratio = 1.0f + std::pow(v, 2.0f) * 19.0f;
                return string::f("%.1f:1", ratio);
            }
        };
        configParam<RatioParamQuantity>(RATIO_PARAM, 0.f, 1.f, 0.397f, "Ratio");  // Default: 4:1

        configParam(DRY_WET_PARAM, 0.f, 1.f, 1.f, "Dry/Wet", "%", 0.f, 100.f);  // Default: 100% wet

        configParam(DISPLAY_ENABLE_PARAM, 0.f, 1.f, 1.f, "Display Enable");  // Display toggle (default: ON)

        // I/O configuration
        configInput(LEFT_INPUT, "Left");
        configInput(RIGHT_INPUT, "Right");
        configInput(SIDECHAIN_INPUT, "Sidechain");
        configOutput(LEFT_OUTPUT, "Left");
        configOutput(RIGHT_OUTPUT, "Right");

        // VCV Rack engine-level bypass (right-click menu)
        configBypass(LEFT_INPUT, LEFT_OUTPUT);
        configBypass(RIGHT_INPUT, RIGHT_OUTPUT);

        lightDivider.setDivision(256);  // Update LEDs every 256 samples (187.5Hz at 48kHz)

        // Initialize compressor engine (default: VCA)
        setCompressorType(VCA_TYPE);
    }

    ~C1COMP() {
        if (comp) {
            delete comp;
            comp = nullptr;
        }
    }

    void onReset() override {
        // Reset all parameters to defaults
        Module::onReset();

        // Reset context menu settings to defaults
        inputGainDb = 0.0f;      // 0dB
        outputGainDb = 0.0f;     // 0dB
        autoMakeup = false;      // Off
        compressorType = VCA_TYPE;  // VCA (SSL G)
        use10VReference = false; // 5V reference
        vuMeterBarMode = false;  // Dot mode (off)
        kneeOverride = -1.0f;    // Auto

        // Ensure engine is updated to default type
        if (lastCompressorType != compressorType) {
            setCompressorType(compressorType);
            lastCompressorType = compressorType;
        }
    }

    void onRandomize(const RandomizeEvent& e) override {
        (void)e;  // Suppress unused parameter warning
        // Disable randomize - do nothing
    }

    void setCompressorType(int type) {
        // Delete old engine
        if (comp) {
            delete comp;
            comp = nullptr;
        }

        // Create new engine based on type
        compressorType = type;
        switch (type) {
            case VCA_TYPE:
                comp = new VCACompressor();
                break;
            case FET_TYPE:
                comp = new FETCompressor();
                break;
            case OPTICAL_TYPE:
                comp = new OpticalCompressor();
                break;
            case VARIMU_TYPE:
                comp = new VariMuCompressor();
                break;
            default:
                comp = new VCACompressor();
                compressorType = VCA_TYPE;
                break;
        }
    }

    void process(const ProcessArgs& args) override {
        if (!comp) return;  // Safety check

        // Check if compressor type has changed (from context menu)
        if (compressorType != lastCompressorType) {
            setCompressorType(compressorType);
            lastCompressorType = compressorType;
        }

        // Set sample rate (recalculates attack/release coefficients if changed)
        comp->setSampleRate(args.sampleRate);

        // Calculate peak decay coefficient (300ms time constant)
        if (peakDecayCoeff == 0.0f) {
            peakDecayCoeff = std::exp(-1.0f / (0.3f * args.sampleRate));
        }

        // Get bypass state
        bool bypassed = params[BYPASS_PARAM].getValue() > 0.5f;

        // Update LEDs at reduced rate (every 256 samples = 187.5Hz at 48kHz)
        bool updateLights = lightDivider.process();
        if (updateLights) {
            lights[BYPASS_LIGHT].setBrightness(bypassed ? 0.65f : 0.0f);
        }

        // Get inputs (VCV Rack ±10V or ±5V → ±1.0 normalized)
        float inputScaling = use10VReference ? 10.0f : 5.0f;
        float inputGainLin = std::pow(10.0f, inputGainDb / 20.0f);  // dB to linear
        float inL = (inputs[LEFT_INPUT].getVoltage() / inputScaling) * inputGainLin;
        float inR = inputs[RIGHT_INPUT].isConnected()
                    ? (inputs[RIGHT_INPUT].getVoltage() / inputScaling) * inputGainLin
                    : inL;  // Mono normalling

        // Update input peak meters (before processing) - feed zeros if display disabled for graceful decay
        bool displayEnabled = params[DISPLAY_ENABLE_PARAM].getValue() > 0.5f;
        updatePeakMeter(displayEnabled ? std::abs(inL) : 0.0f, peakInputLeft);
        updatePeakMeter(displayEnabled ? std::abs(inR) : 0.0f, peakInputRight);

        if (bypassed) {
            // Bypass: pass through with output gain applied
            float outputGainLin = std::pow(10.0f, outputGainDb / 20.0f);
            outputs[LEFT_OUTPUT].setVoltage(inL * outputGainLin * inputScaling);
            outputs[RIGHT_OUTPUT].setVoltage(inR * outputGainLin * inputScaling);

            // Reset VU meter and decay GR meter
            peakGR = peakGR * peakDecayCoeff;  // Decay GR meter
            if (updateLights) {
                for (int i = 0; i < 11; i++) {
                    lights[VU_LIGHT_0 + i].setBrightness(0.0f);
                }
            }

            // Update output meters in bypass - feed zeros if display disabled
            updatePeakMeter(displayEnabled ? std::abs(inL * outputGainLin) : 0.0f, peakOutputLeft);
            updatePeakMeter(displayEnabled ? std::abs(inR * outputGainLin) : 0.0f, peakOutputRight);
            return;
        }

        // Update compressor parameters
        updateCompressorParameters();

        // DSP processing
        float dryL = inL, dryR = inR;
        float wetL, wetR;

        // Sidechain input: dual-purpose (audio, CV, gate, trigger)
        if (inputs[SIDECHAIN_INPUT].getChannels() > 0) {
            float scSignal = inputs[SIDECHAIN_INPUT].getVoltage();
            float scLevel = std::abs(scSignal);  // Rectify for detection
            comp->processStereoWithKey(inL, inR, scLevel, &wetL, &wetR);
        } else {
            comp->processStereo(inL, inR, &wetL, &wetR);
        }

        // Parallel compression (dry/wet mix) - with CV modulation from COM-X
        float mixCVMod = 0.0f;
        if (rightExpander.module && rightExpander.module->model == modelC1COMPCV) {
            C1COMPExpanderMessage* msg = (C1COMPExpanderMessage*)(rightExpander.module->leftExpander.consumerMessage);
            mixCVMod = msg->mixCV;  // -1.0 to +1.0
        }
        float mix = clamp(params[DRY_WET_PARAM].getValue() + mixCVMod, 0.0f, 1.0f);
        float outL = (1.0f - mix) * dryL + mix * wetL;
        float outR = (1.0f - mix) * dryR + mix * wetR;

        // Apply output gain and convert back to voltage
        float outputGainLin = std::pow(10.0f, outputGainDb / 20.0f);  // dB to linear
        outputs[LEFT_OUTPUT].setVoltage(outL * outputGainLin * inputScaling);
        outputs[RIGHT_OUTPUT].setVoltage(outR * outputGainLin * inputScaling);

        // Update GR meter (0dB to -20dB, inverted display) - feed zero if display disabled
        float gr = comp->getGainReduction();  // Returns dB (negative values = gain reduction)
        float grNorm = displayEnabled ? clamp(-gr / 20.0f, 0.0f, 1.0f) : 0.0f;  // Normalize: 0dB=0.0, -20dB=1.0
        // GR meter: instant attack, exponential decay
        if (grNorm > peakGR) {
            peakGR = grNorm;
        } else {
            peakGR = peakGR * peakDecayCoeff;
        }

        // Update output peak meters (after processing with output gain) - feed zeros if display disabled
        updatePeakMeter(displayEnabled ? std::abs(outL * outputGainLin) : 0.0f, peakOutputLeft);
        updatePeakMeter(displayEnabled ? std::abs(outR * outputGainLin) : 0.0f, peakOutputRight);

        // Update VU meter (gain reduction display) at reduced rate
        if (updateLights) {
            updateVUMeter();
        }
    }

    void updatePeakMeter(float input, float& peak) {
        // Convert input to dB range: -60dB to +6dB
        float inputDb = -60.0f;
        if (input > 0.0001f) {
            inputDb = 20.0f * std::log10(input / 5.0f);
        }
        inputDb = clamp(inputDb, -60.0f, 6.0f);

        // Normalize to 0.0-1.0
        float inputNorm = (inputDb + 60.0f) / 66.0f;

        // Peak detection: instant attack, exponential decay
        if (inputNorm > peak) {
            peak = inputNorm;  // Instant attack
        } else {
            peak = peak * peakDecayCoeff;  // Exponential decay (300ms)
        }
    }

    void updateCompressorParameters() {
        // Read CV modulation from COM-X expander (if connected)
        float ratioCVMod = 0.0f;
        float thresholdCVMod = 0.0f;
        float releaseCVMod = 0.0f;

        if (rightExpander.module && rightExpander.module->model == modelC1COMPCV) {
            C1COMPExpanderMessage* msg = (C1COMPExpanderMessage*)(rightExpander.module->leftExpander.consumerMessage);
            ratioCVMod = msg->ratioCV;              // -1.0 to +1.0
            thresholdCVMod = msg->thresholdCV * 30.0f;  // ±30dB range
            releaseCVMod = msg->releaseCV * 0.89f;  // Scale to 0-89% range (avoid AUTO mode)
            // mixCV is read separately in process() where it's used
        }

        // Attack: 6 discrete snap positions (SSL G-style)
        int attackIndex = (int)std::round(params[ATTACK_PARAM].getValue());  // 0-5 (already snapped)
        attackIndex = clamp(attackIndex, 0, 5);
        float attack = attackValues[attackIndex];
        comp->setAttack(attack);

        // Release: Continuous 100ms-1200ms (0-90%) or AUTO (90-100%)
        // CV modulation limited to 0-89% range to preserve AUTO mode access
        float releaseRaw = clamp(params[RELEASE_PARAM].getValue() + releaseCVMod, 0.0f, 0.89f);  // 0.0 to 0.89
        if (releaseRaw >= 0.9f) {
            // AUTO zone (90-100%)
            comp->setAutoRelease(true);
        } else {
            // Continuous zone (0-90%) - logarithmic scaling for musical control
            comp->setAutoRelease(false);
            float normalizedRelease = releaseRaw / 0.9f;  // Rescale 0-0.9 to 0-1
            // Logarithmic mapping: 100ms to 1200ms
            // log(1200/100) = log(12) ≈ 2.485
            float release = 100.0f * std::pow(12.0f, normalizedRelease);
            comp->setRelease(release);
        }

        // Threshold: -20dB to +10dB (SSL G-series range)
        float thresholdBase = rescale(params[THRESHOLD_PARAM].getValue(), 0.0f, 1.0f, -20.0f, 10.0f);
        float threshold = clamp(thresholdBase + thresholdCVMod, -20.0f, 10.0f);
        comp->setThreshold(threshold);

        // Ratio: 1:1 to 20:1 (logarithmic taper for musical control)
        // Musical ratios: 2:1 (25%), 4:1 (50%), 8:1 (75%), 20:1 (100%)
        float ratioParam = clamp(params[RATIO_PARAM].getValue() + ratioCVMod, 0.0f, 1.0f);  // 0.0 to 1.0
        float ratio = 1.0f + std::pow(ratioParam, 2.0f) * 19.0f;
        comp->setRatio(ratio);

        // Makeup gain
        if (autoMakeup) {
            // Simple auto-makeup: compensate for threshold
            float autoGainDb = -threshold * 0.5f;
            comp->setMakeup(autoGainDb);
        } else {
            comp->setMakeup(0.0f);
        }

        // Knee override
        comp->setKnee(kneeOverride);  // -1 = use engine defaults, 0-12 = override
    }

    void updateVUMeter() {
        float gr = comp->getGainReduction();  // Returns dB (negative values = gain reduction)

        // Simple linear mapping: 0dB to -20dB across 11 LEDs
        // LED 10 (right) = 0dB (no compression), LED 0 (left) = -20dB (heavy compression)
        float grAbs = clamp(-gr, 0.0f, 20.0f);  // Convert to positive: 0 to 20dB range

        // Linear: 2dB per LED
        int activeLED = 10 - (int)(grAbs / 2.0f);  // 0dB→LED10, -2dB→LED9, -4dB→LED8, etc.
        activeLED = clamp(activeLED, 0, 10);

        if (vuMeterBarMode) {
            // Bar mode: all LEDs up to current GR light up
            for (int i = 0; i < 11; i++) {
                lights[VU_LIGHT_0 + i].setBrightness(i <= activeLED ? 1.0f : 0.0f);
            }
        } else {
            // Dot mode: only current GR LED lights up
            for (int i = 0; i < 11; i++) {
                lights[VU_LIGHT_0 + i].setBrightness(i == activeLED ? 1.0f : 0.0f);
            }
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "compressorType", json_integer(compressorType));
        json_object_set_new(rootJ, "vuMeterBarMode", json_boolean(vuMeterBarMode));
        json_object_set_new(rootJ, "autoMakeup", json_boolean(autoMakeup));
        json_object_set_new(rootJ, "use10VReference", json_boolean(use10VReference));
        json_object_set_new(rootJ, "inputGainDb", json_real(inputGainDb));
        json_object_set_new(rootJ, "outputGainDb", json_real(outputGainDb));
        json_object_set_new(rootJ, "kneeOverride", json_real(kneeOverride));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* compressorTypeJ = json_object_get(rootJ, "compressorType");
        if (compressorTypeJ) {
            int type = json_integer_value(compressorTypeJ);
            setCompressorType(type);  // Recreate appropriate engine
            lastCompressorType = type;
        }

        json_t* vuMeterBarModeJ = json_object_get(rootJ, "vuMeterBarMode");
        if (vuMeterBarModeJ)
            vuMeterBarMode = json_boolean_value(vuMeterBarModeJ);

        json_t* autoMakeupJ = json_object_get(rootJ, "autoMakeup");
        if (autoMakeupJ)
            autoMakeup = json_boolean_value(autoMakeupJ);

        json_t* use10VJ = json_object_get(rootJ, "use10VReference");
        if (use10VJ)
            use10VReference = json_boolean_value(use10VJ);

        json_t* inputGainJ = json_object_get(rootJ, "inputGainDb");
        if (inputGainJ)
            inputGainDb = json_real_value(inputGainJ);

        json_t* outputGainJ = json_object_get(rootJ, "outputGainDb");
        if (outputGainJ)
            outputGainDb = json_real_value(outputGainJ);

        json_t* kneeOverrideJ = json_object_get(rootJ, "kneeOverride");
        if (kneeOverrideJ)
            kneeOverride = json_real_value(kneeOverrideJ);
    }
};

struct C1COMPWidget : ModuleWidget {
    C1COMPWidget(C1COMP* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/C1COMP.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // COMPRESSOR title text (TC style)
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
                            nvgText(args.vg, dx * 0.5f, dy * 0.5f, "COMP", NULL);
                        }
                    }
                }

                nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
                nvgText(args.vg, 0, 0, "COMP", NULL);
            }
        };

        TitleLabel* titleLabel = new TitleLabel();
        titleLabel->box.pos = Vec(60, 10);  // X=60 (center of 8HP module), Y=10 (same as ORDER)
        titleLabel->box.size = Vec(104, 20);
        addChild(titleLabel);

        // TC Button Widgets (matching Shape/C1EQ implementation)
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
        C1WhiteRoundButton* bypassButton = createParamCentered<C1WhiteRoundButton>(Vec(23, 26), module, C1COMP::BYPASS_PARAM);
        addParam(bypassButton);
        bypassButton->getLight()->module = module;
        bypassButton->getLight()->firstLightId = C1COMP::BYPASS_LIGHT;

        // VU Meter Labels using NanoVG (matching Shape module)
        struct VULabel : Widget {
            std::string text;
            VULabel(std::string t) : text(t) {}
            void draw(const DrawArgs& args) override {
                nvgFontSize(args.vg, 5.0f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 180));
                nvgText(args.vg, box.size.x/2, box.size.y/2, text.c_str(), NULL);
            }
        };

        VULabel* minLabel = new VULabel("-20");
        minLabel->box.pos = Vec(14.5, 83);
        minLabel->box.size = Vec(10, 6);
        addChild(minLabel);

        VULabel* maxLabel = new VULabel("0");
        maxLabel->box.pos = Vec(96, 83);
        maxLabel->box.size = Vec(8, 6);
        addChild(maxLabel);

        VULabel* centerLabel = new VULabel("6");
        centerLabel->box.pos = Vec(56, 83);
        centerLabel->box.size = Vec(8, 6);
        addChild(centerLabel);

        VULabel* threeLabel = new VULabel("3");
        threeLabel->box.pos = Vec(72, 83);
        threeLabel->box.size = Vec(8, 6);
        addChild(threeLabel);

        VULabel* tenLabel = new VULabel("10");
        tenLabel->box.pos = Vec(40, 83);
        tenLabel->box.size = Vec(8, 6);
        addChild(tenLabel);

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

        // Left column encoders with LED rings - C1Knob280
        addParam(createParamCentered<C1Knob280>(Vec(35, 145), module, C1COMP::RATIO_PARAM));
        LedRingOverlay* ratioRing = new LedRingOverlay(module, C1COMP::RATIO_PARAM);
        ratioRing->box.pos = Vec(35 - 25, 145 - 25);
        addChild(ratioRing);
        ControlLabel* ratioLabel = new ControlLabel("RATIO");
        ratioLabel->box.pos = Vec(35, 169);  // 24px clearance below encoder
        ratioLabel->box.size = Vec(40, 10);
        addChild(ratioLabel);

        addParam(createParamCentered<C1Knob280>(Vec(35, 195), module, C1COMP::DRY_WET_PARAM));
        LedRingOverlay* dryWetRing = new LedRingOverlay(module, C1COMP::DRY_WET_PARAM);
        dryWetRing->box.pos = Vec(35 - 25, 195 - 25);
        addChild(dryWetRing);
        ControlLabel* dryWetLabel = new ControlLabel("DRY/WET");
        dryWetLabel->box.pos = Vec(35, 219);  // 24px clearance below encoder
        dryWetLabel->box.size = Vec(40, 10);
        addChild(dryWetLabel);

        // Right column encoders with LED rings - C1Knob280
        addParam(createParamCentered<C1SnapKnob280>(Vec(85, 125), module, C1COMP::ATTACK_PARAM));
        AttackLedRing* attackRing = new AttackLedRing(module, C1COMP::ATTACK_PARAM);
        attackRing->box.pos = Vec(85 - 25, 125 - 25);
        addChild(attackRing);
        ControlLabel* attackLabel = new ControlLabel("ATTACK");
        attackLabel->box.pos = Vec(85, 149);  // 24px clearance below encoder
        attackLabel->box.size = Vec(40, 10);
        addChild(attackLabel);

        addParam(createParamCentered<C1Knob280>(Vec(85, 175), module, C1COMP::RELEASE_PARAM));
        ReleaseLedRing* releaseRing = new ReleaseLedRing(module, C1COMP::RELEASE_PARAM);
        releaseRing->box.pos = Vec(85 - 25, 175 - 25);
        addChild(releaseRing);
        ControlLabel* releaseLabel = new ControlLabel("RELEASE");
        releaseLabel->box.pos = Vec(85, 199);  // 24px clearance below encoder
        releaseLabel->box.size = Vec(40, 10);
        addChild(releaseLabel);

        addParam(createParamCentered<C1Knob280>(Vec(85, 225), module, C1COMP::THRESHOLD_PARAM));
        LedRingOverlay* thresholdRing = new LedRingOverlay(module, C1COMP::THRESHOLD_PARAM);
        thresholdRing->box.pos = Vec(85 - 25, 225 - 25);
        addChild(thresholdRing);
        ControlLabel* thresholdLabel = new ControlLabel("THRESHOLD");
        thresholdLabel->box.pos = Vec(85, 249);  // 24px clearance below encoder
        thresholdLabel->box.size = Vec(40, 10);
        addChild(thresholdLabel);

        // I/O jacks at exactly the same positions as Shape module
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(30, 284), module, C1COMP::LEFT_INPUT));
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(30, 314), module, C1COMP::RIGHT_INPUT));
        addInput(createInputCentered<ThemedPJ301MPort>(Vec(60, 299), module, C1COMP::SIDECHAIN_INPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(90, 284), module, C1COMP::LEFT_OUTPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(90, 314), module, C1COMP::RIGHT_OUTPUT));

        // I/O Labels - exact same styling and positioning as Shape module
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
        outLabel->box.pos = Vec(90, 330);
        outLabel->box.size = Vec(20, 10);
        addChild(outLabel);

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
        scLabel->box.size = Vec(20, 10);
        addChild(scLabel);

        // VU Meter LEDs (matching Shape module positions)
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(20, 91), module, C1COMP::VU_LIGHT_0));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(28, 91), module, C1COMP::VU_LIGHT_1));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(36, 91), module, C1COMP::VU_LIGHT_2));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(44, 91), module, C1COMP::VU_LIGHT_3));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(52, 91), module, C1COMP::VU_LIGHT_4));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(60, 91), module, C1COMP::VU_LIGHT_5));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(68, 91), module, C1COMP::VU_LIGHT_6));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(76, 91), module, C1COMP::VU_LIGHT_7));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(84, 91), module, C1COMP::VU_LIGHT_8));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(92, 91), module, C1COMP::VU_LIGHT_9));
        addChild(createLightCentered<TinyLight<RedLight>>(Vec(100, 91), module, C1COMP::VU_LIGHT_10));

        // Display area widgets (96×54px display area at 12, 41)
        // CompressorTypeSwitchWidget at top (4 rectangles for VCA/FET/Optical/Vari-Mu)
        CompressorTypeSwitchWidget* compTypeSwitches = new CompressorTypeSwitchWidget(
            module,
            module ? &module->compressorType : nullptr
        );
        compTypeSwitches->box.pos = Vec(14, 43);  // 2px from display edges
        compTypeSwitches->box.size = Vec(92, 12);  // Full width, 12px height for switches
        addChild(compTypeSwitches);

        // Compressor type label - displays current engine name
        struct CompressorTypeLabel : Widget {
            C1COMP* module;
            CompressorTypeLabel(C1COMP* m) : module(m) {}
            void draw(const DrawArgs& args) override {
                if (!module) return;

                const char* typeNames[4] = {"VCA", "FET", "OPTICAL", "VARI-MU"};
                int type = module->compressorType;
                type = clamp(type, 0, 3);

                nvgFontSize(args.vg, 6.0f);
                nvgFontFaceId(args.vg, APP->window->uiFont->handle);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor(args.vg, nvgRGBA(0xFF, 0xC0, 0x50, 200));  // Amber
                nvgText(args.vg, 0, box.size.y / 2, typeNames[type], NULL);
            }
        };

        CompressorTypeLabel* typeLabel = new CompressorTypeLabel(module);
        typeLabel->box.pos = Vec(46, 45);  // 1px above centered position
        typeLabel->box.size = Vec(50, 6);
        addChild(typeLabel);

        // Peak meter displays (3 meters: IN stereo, GR mono inverted, OUT stereo)
        // Positioned with 2.5px clearance from switches (top) and grey box (bottom)
        // IN meter (stereo)
        PeakMeterDisplay* inMeter = new PeakMeterDisplay(
            module,
            true,
            false,
            module ? &module->peakInputLeft : nullptr,
            module ? &module->peakInputRight : nullptr,
            0.5f,   // 0.5 second peak hold time
            "IN"    // Meter label
        );
        inMeter->box.pos = Vec(16, 56);  // 2.5px clearance from switches and bottom
        inMeter->box.size = Vec(88, 7.5);
        addChild(inMeter);

        // GR meter (mono inverted) - positioned below IN meter
        PeakMeterDisplay* grMeter = new PeakMeterDisplay(
            module,
            false,
            true,
            module ? &module->peakGR : nullptr,
            nullptr,
            0.1f,   // 0.1 second peak hold time for GR meter
            "GR"    // Meter label
        );
        grMeter->box.pos = Vec(16, 63.5);  // 7.5px below IN meter
        grMeter->box.size = Vec(88, 7.5);
        addChild(grMeter);

        // OUT meter (stereo) - positioned below GR meter
        PeakMeterDisplay* outMeter = new PeakMeterDisplay(
            module,
            true,
            false,
            module ? &module->peakOutputLeft : nullptr,
            module ? &module->peakOutputRight : nullptr,
            0.5f,   // 0.5 second peak hold time
            "OUT"   // Meter label
        );
        outMeter->box.pos = Vec(16, 71);  // 7.5px below GR meter
        outMeter->box.size = Vec(88, 7.5);
        addChild(outMeter);

        // Display toggle switch (upper right corner, matching ChanIn/Shape style)
        struct DisplayToggleSwitch : widget::OpaqueWidget {
            C1COMP* module = nullptr;
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
                bool displayOn = module ? (module->params[C1COMP::DISPLAY_ENABLE_PARAM].getValue() > 0.5f) : true;

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
                    float currentValue = module->params[C1COMP::DISPLAY_ENABLE_PARAM].getValue();
                    module->params[C1COMP::DISPLAY_ENABLE_PARAM].setValue(currentValue > 0.5f ? 0.0f : 1.0f);

                    e.consume(this);
                }
            }
        };

        DisplayToggleSwitch* displayToggle = new DisplayToggleSwitch();
        displayToggle->module = module;
        displayToggle->box.pos = Vec(96, 43);  // Upper right corner of display area
        displayToggle->box.size = Vec(12, 12);
        addChild(displayToggle);

        // Twisted Cable logo - using shared widget
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::FULL, module);
        tcLogo->box.pos = Vec(60, 355);
        addChild(tcLogo);
    }

    void appendContextMenu(Menu* menu) override {
        C1COMP* module = getModule<C1COMP>();
        if (!module)
            return;

        menu->addChild(new MenuSeparator);

        // Auto Makeup Gain
        menu->addChild(createBoolPtrMenuItem("Auto Makeup Gain", "", &module->autoMakeup));

        menu->addChild(new MenuSeparator);

        // Gain section
        menu->addChild(createMenuLabel("Gain"));

        struct InputGainQuantity : Quantity {
            C1COMP* module;
            InputGainQuantity(C1COMP* m) : module(m) {}
            void setValue(float value) override { module->inputGainDb = clamp(value, -24.0f, 24.0f); }
            float getValue() override { return module->inputGainDb; }
            float getMinValue() override { return -24.0f; }
            float getMaxValue() override { return 24.0f; }
            float getDefaultValue() override { return 0.0f; }
            std::string getLabel() override { return "Input"; }
            std::string getUnit() override { return " dB"; }
        };
        struct InputGainSlider : ui::Slider {
            C1COMP* module;
            InputGainSlider(C1COMP* m) : module(m) {
                box.size.x = 200.0f;
                quantity = new InputGainQuantity(module);
            }
            ~InputGainSlider() {
                delete quantity;
            }
        };
        menu->addChild(new InputGainSlider(module));

        struct OutputGainQuantity : Quantity {
            C1COMP* module;
            OutputGainQuantity(C1COMP* m) : module(m) {}
            void setValue(float value) override { module->outputGainDb = clamp(value, -24.0f, 24.0f); }
            float getValue() override { return module->outputGainDb; }
            float getMinValue() override { return -24.0f; }
            float getMaxValue() override { return 24.0f; }
            float getDefaultValue() override { return 0.0f; }
            std::string getLabel() override { return "Output"; }
            std::string getUnit() override { return " dB"; }
        };
        struct OutputGainSlider : ui::Slider {
            C1COMP* module;
            OutputGainSlider(C1COMP* m) : module(m) {
                box.size.x = 200.0f;
                quantity = new OutputGainQuantity(module);
            }
            ~OutputGainSlider() {
                delete quantity;
            }
        };
        menu->addChild(new OutputGainSlider(module));

        menu->addChild(new MenuSeparator);

        // Knee Override Slider
        struct KneeQuantity : Quantity {
            C1COMP* module;
            KneeQuantity(C1COMP* m) : module(m) {}

            void setValue(float value) override {
                if (value <= 0.0f) {
                    module->kneeOverride = -1.0f;  // Snap to Auto
                } else {
                    module->kneeOverride = clamp(value, 0.0f, 12.0f);
                }
            }

            float getValue() override {
                return (module->kneeOverride < 0.0f) ? 0.0f : module->kneeOverride;
            }

            float getMinValue() override { return 0.0f; }
            float getMaxValue() override { return 12.0f; }
            float getDefaultValue() override { return 0.0f; }  // Auto by default

            std::string getLabel() override { return "Knee"; }

            std::string getUnit() override { return " dB"; }

            std::string getDisplayValueString() override {
                if (module->kneeOverride < 0.0f) {
                    return "Auto";
                } else {
                    return string::f("%.1f", module->kneeOverride);
                }
            }

            int getDisplayPrecision() override { return 1; }
        };

        struct KneeSlider : ui::Slider {
            C1COMP* module;
            KneeSlider(C1COMP* m) : module(m) {
                box.size.x = 200.0f;
                quantity = new KneeQuantity(module);
            }
            ~KneeSlider() {
                delete quantity;
            }
        };
        menu->addChild(new KneeSlider(module));

        menu->addChild(new MenuSeparator);

        // Compressor Type Selection
        menu->addChild(createIndexPtrSubmenuItem("Compressor Type", {
            "VCA (SSL G)",
            "FET (1176)",
            "Optical (LA-2A)",
            "Vari-Mu (Fairchild)"
        }, &module->compressorType));

        menu->addChild(new MenuSeparator);

        // Input Reference Level
        menu->addChild(createSubmenuItem("Input Reference Level", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("0dBFS = 5V", "",
                    [=]() { return !module->use10VReference; },
                    [=]() { module->use10VReference = false; }
                ));
                menu->addChild(createCheckMenuItem("0dBFS = 10V", "",
                    [=]() { return module->use10VReference; },
                    [=]() { module->use10VReference = true; }
                ));
            }
        ));

        menu->addChild(new MenuSeparator);

        // VU Meter Mode
        menu->addChild(createSubmenuItem("VU Meter Mode", "",
            [=](Menu* menu) {
                menu->addChild(createCheckMenuItem("Dot Mode", "",
                    [=]() { return !module->vuMeterBarMode; },
                    [=]() { module->vuMeterBarMode = false; }
                ));
                menu->addChild(createCheckMenuItem("Bar Mode", "",
                    [=]() { return module->vuMeterBarMode; },
                    [=]() { module->vuMeterBarMode = true; }
                ));
            }
        ));
    }
};

} // namespace

Model* modelC1COMP = createModel<C1COMP, C1COMPWidget>("C1COMP");