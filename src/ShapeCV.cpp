#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include <cmath>

namespace {

// Forward declaration for message struct (matches Shape.cpp definition)
struct ShapeExpanderMessage {
    float thresholdCV;    // -1.0 to +1.0 (attenuated, represents ±60dB range)
    float sustainCV;      // -1.0 to +1.0 (attenuated, 0-300ms range)
    float releaseCV;      // -1.0 to +1.0 (attenuated, 0.1s-4s range)
    float modeCV;         // Gate: >1V = hard gate mode
};

// Dark PJ301M port
using DarkPJ301MPort = rack::componentlibrary::DarkPJ301MPort;

// Custom TinySimpleLight with 20% outer glow (halo)
template <typename TBase = rack::componentlibrary::YellowLight>
struct TinySimpleLightHalf : rack::componentlibrary::TinySimpleLight<TBase> {
    TinySimpleLightHalf() {
        this->bgColor = nvgRGBA(0x33, 0x33, 0x33, 0x33); // 20% alpha
        this->borderColor = nvgRGBA(0, 0, 0, 11);        // 20% alpha
    }
};

struct ShapeCV : Module {
    enum ParamIds {
        THRESHOLD_ATTEN_PARAM,    // Threshold CV attenuverter (-1 to +1)
        SUSTAIN_ATTEN_PARAM,      // Sustain CV attenuverter (-1 to +1)
        RELEASE_ATTEN_PARAM,      // Release CV attenuverter (-1 to +1)
        PARAMS_LEN
    };

    enum InputIds {
        THRESHOLD_CV_INPUT,
        SUSTAIN_CV_INPUT,
        RELEASE_CV_INPUT,
        MODE_CV_INPUT,
        INPUTS_LEN
    };

    enum LightIds {
        THRESHOLD_ATTEN_TOP_LIGHT,
        THRESHOLD_ATTEN_LEFT_LIGHT,
        THRESHOLD_ATTEN_RIGHT_LIGHT,
        SUSTAIN_ATTEN_TOP_LIGHT,
        SUSTAIN_ATTEN_LEFT_LIGHT,
        SUSTAIN_ATTEN_RIGHT_LIGHT,
        RELEASE_ATTEN_TOP_LIGHT,
        RELEASE_ATTEN_LEFT_LIGHT,
        RELEASE_ATTEN_RIGHT_LIGHT,
        LIGHTS_LEN
    };

    // Message buffers for communication with SHAPE
    ShapeExpanderMessage leftMessages[2];  // Double buffer for thread safety

    // CV smoothing filters (1ms time constant to prevent zipper noise)
    dsp::TExponentialFilter<float> thresholdCVFilter;
    dsp::TExponentialFilter<float> sustainCVFilter;
    dsp::TExponentialFilter<float> releaseCVFilter;

    // Connection indicator fade (0.0 = white, 1.0 = amber)
    float connectionFade = 0.0f;

    ShapeCV() {
        config(PARAMS_LEN, INPUTS_LEN, 0, LIGHTS_LEN);

        // Configure attenuverter parameters (bipolar -1 to +1, default 0%)
        configParam(THRESHOLD_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Threshold CV Amount", "%", 0.0f, 100.0f);
        configParam(SUSTAIN_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Sustain CV Amount", "%", 0.0f, 100.0f);
        configParam(RELEASE_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Release CV Amount", "%", 0.0f, 100.0f);

        // Configure CV inputs
        configInput(THRESHOLD_CV_INPUT, "Threshold CV");
        configInput(SUSTAIN_CV_INPUT, "Sustain CV");
        configInput(RELEASE_CV_INPUT, "Release CV");
        configInput(MODE_CV_INPUT, "Hard Gate Mode CV");

        // Initialize expander messages
        leftExpander.producerMessage = &leftMessages[0];
        leftExpander.consumerMessage = &leftMessages[1];

        // Initialize CV smoothing filters (~1ms time constant)
        thresholdCVFilter.setLambda(1000.0f);
        sustainCVFilter.setLambda(1000.0f);
        releaseCVFilter.setLambda(1000.0f);
    }

    void process(const ProcessArgs& args) override {
        // Check if SHAPE is connected to the left
        if (leftExpander.module && leftExpander.module->model == modelShape) {
            // VCV Rack automatically points leftExpander.producerMessage to SHAPE's consumerMessage
            ShapeExpanderMessage* msg = (ShapeExpanderMessage*)leftExpander.producerMessage;

            // Read CV inputs with smoothing and attenuverters
            // Threshold CV: ±10V input → -1 to +1 (attenuated, ±60dB range)
            if (inputs[THRESHOLD_CV_INPUT].isConnected()) {
                float raw = inputs[THRESHOLD_CV_INPUT].getVoltage();
                float smoothed = thresholdCVFilter.process(args.sampleTime, raw);
                msg->thresholdCV = (smoothed / 10.0f) * params[THRESHOLD_ATTEN_PARAM].getValue();
            } else {
                msg->thresholdCV = 0.0f;
            }

            // Sustain CV: ±10V input → -1 to +1 (attenuated, 0-300ms range)
            if (inputs[SUSTAIN_CV_INPUT].isConnected()) {
                float raw = inputs[SUSTAIN_CV_INPUT].getVoltage();
                float smoothed = sustainCVFilter.process(args.sampleTime, raw);
                msg->sustainCV = (smoothed / 10.0f) * params[SUSTAIN_ATTEN_PARAM].getValue();
            } else {
                msg->sustainCV = 0.0f;
            }

            // Release CV: ±10V input → -1 to +1 (attenuated, 0.1s-4s range)
            if (inputs[RELEASE_CV_INPUT].isConnected()) {
                float raw = inputs[RELEASE_CV_INPUT].getVoltage();
                float smoothed = releaseCVFilter.process(args.sampleTime, raw);
                msg->releaseCV = (smoothed / 10.0f) * params[RELEASE_ATTEN_PARAM].getValue();
            } else {
                msg->releaseCV = 0.0f;
            }

            // Mode CV: Gate (>1V = hard gate)
            if (inputs[MODE_CV_INPUT].isConnected()) {
                msg->modeCV = inputs[MODE_CV_INPUT].getVoltage();
            } else {
                msg->modeCV = 0.0f;
            }

            // Flip message buffers
            leftExpander.messageFlipRequested = true;
        }

        // Smooth fade for connection indicator (200ms fade time)
        bool isConnected = leftExpander.module && leftExpander.module->model == modelShape;
        float targetFade = isConnected ? 1.0f : 0.0f;
        float fadeSpeed = 5.0f;  // 5 units/sec = 200ms fade
        connectionFade += (targetFade - connectionFade) * fadeSpeed * args.sampleTime;

        // Update attenuverter position lights (smooth fade between positions)

        // Threshold CV attenuverter
        float thresholdAttenValue = params[THRESHOLD_ATTEN_PARAM].getValue();
        lights[THRESHOLD_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(thresholdAttenValue));
        lights[THRESHOLD_ATTEN_LEFT_LIGHT].setBrightness(thresholdAttenValue < 0.0f ? -thresholdAttenValue : 0.0f);
        lights[THRESHOLD_ATTEN_RIGHT_LIGHT].setBrightness(thresholdAttenValue > 0.0f ? thresholdAttenValue : 0.0f);

        // Sustain CV attenuverter
        float sustainAttenValue = params[SUSTAIN_ATTEN_PARAM].getValue();
        lights[SUSTAIN_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(sustainAttenValue));
        lights[SUSTAIN_ATTEN_LEFT_LIGHT].setBrightness(sustainAttenValue < 0.0f ? -sustainAttenValue : 0.0f);
        lights[SUSTAIN_ATTEN_RIGHT_LIGHT].setBrightness(sustainAttenValue > 0.0f ? sustainAttenValue : 0.0f);

        // Release CV attenuverter
        float releaseAttenValue = params[RELEASE_ATTEN_PARAM].getValue();
        lights[RELEASE_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(releaseAttenValue));
        lights[RELEASE_ATTEN_LEFT_LIGHT].setBrightness(releaseAttenValue < 0.0f ? -releaseAttenValue : 0.0f);
        lights[RELEASE_ATTEN_RIGHT_LIGHT].setBrightness(releaseAttenValue > 0.0f ? releaseAttenValue : 0.0f);
    }
};

struct ShapeCVWidget : ModuleWidget {
    ShapeCVWidget(ShapeCV* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ShapeCV.svg")));

        // No screws - magnetic faceplate

        // TC Logo (compact, same position as C1 module)
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::COMPACT, module);
        tcLogo->box.pos = Vec(22.5, 355);  // Centered for 3HP (45mm / 2 = 22.5)
        addChild(tcLogo);

        // Title label rendered by code below

        // CV Inputs and Attenuverters (vertical layout)
        // 3HP = 45mm width, center at X=22.5

        // Threshold CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 60), module, ShapeCV::THRESHOLD_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 103), module, ShapeCV::THRESHOLD_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 90.6), module, ShapeCV::THRESHOLD_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 111.8), module, ShapeCV::THRESHOLD_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 111.8), module, ShapeCV::THRESHOLD_ATTEN_RIGHT_LIGHT));

        // Sustain CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 130), module, ShapeCV::SUSTAIN_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 173), module, ShapeCV::SUSTAIN_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 160.6), module, ShapeCV::SUSTAIN_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 181.8), module, ShapeCV::SUSTAIN_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 181.8), module, ShapeCV::SUSTAIN_ATTEN_RIGHT_LIGHT));

        // Release CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 200), module, ShapeCV::RELEASE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 243), module, ShapeCV::RELEASE_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 230.6), module, ShapeCV::RELEASE_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 251.8), module, ShapeCV::RELEASE_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 251.8), module, ShapeCV::RELEASE_ATTEN_RIGHT_LIGHT));

        // Mode CV input (no attenuverter - simple gate)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 270), module, ShapeCV::MODE_CV_INPUT));
    }

    void draw(const DrawArgs& args) override {
        ModuleWidget::draw(args);

        // Draw title and labels (matching other modules' style)
        std::shared_ptr<Font> sonoBold = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
        std::shared_ptr<Font> sonoMedium = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));

        // Title (SH on first line, ·X· on second line) - Bold 18pt with black outline
        nvgFontFaceId(args.vg, sonoBold->handle);
        nvgFontSize(args.vg, 18.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        // Black outline for "SH" (Y=10 to match parent module title)
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 10 + dy * 0.5f, "SH", NULL);
                }
            }
        }
        // White fill for "SH"
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 10, "SH", NULL);

        // "·X·" with smooth color fade on dots
        float fade = module ? static_cast<ShapeCV*>(module)->connectionFade : 0.0f;

        // Interpolate color from white (255,255,255) to amber (255,192,80)
        uint8_t dotR = 255;
        uint8_t dotG = (uint8_t)(255 - fade * (255 - 192));  // 255 → 192
        uint8_t dotB = (uint8_t)(255 - fade * (255 - 80));   // 255 → 80
        uint8_t dotA = (uint8_t)(255 - fade * 76);           // 255 → 179 (70% opacity)

        // Black outline for dots
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 30 + dy * 0.5f, "· ·", NULL);
                }
            }
        }
        // Colored dots with opacity fade
        nvgFillColor(args.vg, nvgRGBA(dotR, dotG, dotB, dotA));
        nvgText(args.vg, 22.5, 30, "· ·", NULL);

        // Black outline for X
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 30 + dy * 0.5f, " X ", NULL);
                }
            }
        }
        // White X
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 30, " X ", NULL);

        // Parameter labels - Medium 10pt with black outline
        nvgFontFaceId(args.vg, sonoMedium->handle);
        nvgFontSize(args.vg, 10.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        const char* labels[3] = {"THRES", "SUST", "RELS"};
        float labelY[3] = {80, 150, 220};

        for (int i = 0; i < 3; i++) {
            // Black outline
            nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx != 0 || dy != 0) {
                        nvgText(args.vg, 22.5 + dx * 0.5f, labelY[i] + dy * 0.5f, labels[i], NULL);
                    }
                }
            }
            // White fill
            nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
            nvgText(args.vg, 22.5, labelY[i], labels[i], NULL);
        }

        // Mode label as two lines: "HARD" and "GATE" (aligned with CHI-X "PHASE" at Y=290)
        // Black outline for "HARD"
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 290 + dy * 0.5f, "HARD", NULL);
                }
            }
        }
        // White fill for "HARD"
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 290, "HARD", NULL);

        // Black outline for "GATE"
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 300 + dy * 0.5f, "GATE", NULL);
                }
            }
        }
        // White fill for "GATE"
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 300, "GATE", NULL);
    }
};

} // namespace

Model* modelShapeCV = createModel<ShapeCV, ShapeCVWidget>("ShapeCV");
