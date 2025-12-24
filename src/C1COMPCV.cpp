#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include <cmath>

namespace {

// Forward declaration for message struct (matches C1-COMP.cpp definition)
struct C1COMPExpanderMessage {
    float ratioCV;     // -1.0 to +1.0 (attenuated)
    float thresholdCV; // -1.0 to +1.0 (attenuated)
    float releaseCV;   // -1.0 to +1.0 (attenuated)
    float mixCV;       // -1.0 to +1.0 (attenuated)
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

struct C1COMPCV : Module {
    enum ParamIds {
        RATIO_ATTEN_PARAM,     // Ratio CV attenuverter (-1 to +1)
        THRESHOLD_ATTEN_PARAM, // Threshold CV attenuverter (-1 to +1)
        RELEASE_ATTEN_PARAM,   // Release CV attenuverter (-1 to +1)
        MIX_ATTEN_PARAM,       // Mix CV attenuverter (-1 to +1)
        PARAMS_LEN
    };

    enum InputIds {
        RATIO_CV_INPUT,
        THRESHOLD_CV_INPUT,
        RELEASE_CV_INPUT,
        MIX_CV_INPUT,
        INPUTS_LEN
    };

    enum LightIds {
        RATIO_ATTEN_TOP_LIGHT,
        RATIO_ATTEN_LEFT_LIGHT,
        RATIO_ATTEN_RIGHT_LIGHT,
        THRESHOLD_ATTEN_TOP_LIGHT,
        THRESHOLD_ATTEN_LEFT_LIGHT,
        THRESHOLD_ATTEN_RIGHT_LIGHT,
        RELEASE_ATTEN_TOP_LIGHT,
        RELEASE_ATTEN_LEFT_LIGHT,
        RELEASE_ATTEN_RIGHT_LIGHT,
        MIX_ATTEN_TOP_LIGHT,
        MIX_ATTEN_LEFT_LIGHT,
        MIX_ATTEN_RIGHT_LIGHT,
        LIGHTS_LEN
    };

    // Message buffers for communication with C1-COMP
    C1COMPExpanderMessage leftMessages[2];  // Double buffer for thread safety

    // CV smoothing filters (1ms time constant to prevent zipper noise)
    dsp::TExponentialFilter<float> ratioCVFilter;
    dsp::TExponentialFilter<float> thresholdCVFilter;
    dsp::TExponentialFilter<float> releaseCVFilter;
    dsp::TExponentialFilter<float> mixCVFilter;

    // Connection indicator fade (0.0 = white, 1.0 = amber)
    float connectionFade = 0.0f;

    C1COMPCV() {
        config(PARAMS_LEN, INPUTS_LEN, 0, LIGHTS_LEN);

        // Configure attenuverter parameters (bipolar -1 to +1, default 0%)
        configParam(RATIO_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Ratio CV Amount", "%", 0.0f, 100.0f);
        configParam(THRESHOLD_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Threshold CV Amount", "%", 0.0f, 100.0f);
        configParam(RELEASE_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Release CV Amount", "%", 0.0f, 100.0f);
        configParam(MIX_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Mix CV Amount", "%", 0.0f, 100.0f);

        // Configure CV inputs
        configInput(RATIO_CV_INPUT, "Ratio CV");
        configInput(THRESHOLD_CV_INPUT, "Threshold CV");
        configInput(RELEASE_CV_INPUT, "Release CV");
        configInput(MIX_CV_INPUT, "Mix CV");

        // Initialize expander messages
        leftExpander.producerMessage = &leftMessages[0];
        leftExpander.consumerMessage = &leftMessages[1];

        // Initialize CV smoothing filters (~1ms time constant)
        ratioCVFilter.setLambda(1000.0f);
        thresholdCVFilter.setLambda(1000.0f);
        releaseCVFilter.setLambda(1000.0f);
        mixCVFilter.setLambda(1000.0f);
    }

    void process(const ProcessArgs& args) override {
        // Check if C1-COMP is connected to the left
        if (leftExpander.module && leftExpander.module->model == modelC1COMP) {
            // VCV Rack automatically points leftExpander.producerMessage to C1-COMP's consumerMessage
            C1COMPExpanderMessage* msg = (C1COMPExpanderMessage*)leftExpander.producerMessage;

            // Read CV inputs with smoothing and attenuverters
            // ±10V input → -1 to +1 (attenuated)
            if (inputs[RATIO_CV_INPUT].isConnected()) {
                float raw = inputs[RATIO_CV_INPUT].getVoltage();
                float smoothed = ratioCVFilter.process(args.sampleTime, raw);
                msg->ratioCV = (smoothed / 10.0f) * params[RATIO_ATTEN_PARAM].getValue();
            } else {
                msg->ratioCV = 0.0f;
            }

            if (inputs[THRESHOLD_CV_INPUT].isConnected()) {
                float raw = inputs[THRESHOLD_CV_INPUT].getVoltage();
                float smoothed = thresholdCVFilter.process(args.sampleTime, raw);
                msg->thresholdCV = (smoothed / 10.0f) * params[THRESHOLD_ATTEN_PARAM].getValue();
            } else {
                msg->thresholdCV = 0.0f;
            }

            if (inputs[RELEASE_CV_INPUT].isConnected()) {
                float raw = inputs[RELEASE_CV_INPUT].getVoltage();
                float smoothed = releaseCVFilter.process(args.sampleTime, raw);
                msg->releaseCV = (smoothed / 10.0f) * params[RELEASE_ATTEN_PARAM].getValue();
            } else {
                msg->releaseCV = 0.0f;
            }

            if (inputs[MIX_CV_INPUT].isConnected()) {
                float raw = inputs[MIX_CV_INPUT].getVoltage();
                float smoothed = mixCVFilter.process(args.sampleTime, raw);
                msg->mixCV = (smoothed / 10.0f) * params[MIX_ATTEN_PARAM].getValue();
            } else {
                msg->mixCV = 0.0f;
            }

            // Flip message buffers
            leftExpander.messageFlipRequested = true;
        }

        // Smooth fade for connection indicator (200ms fade time)
        bool isConnected = leftExpander.module && leftExpander.module->model == modelC1COMP;
        float targetFade = isConnected ? 1.0f : 0.0f;
        float fadeSpeed = 5.0f;  // 5 units/sec = 200ms fade
        connectionFade += (targetFade - connectionFade) * fadeSpeed * args.sampleTime;

        // Update attenuverter position lights (smooth fade between positions)

        // Ratio CV attenuverter
        float ratioAttenValue = params[RATIO_ATTEN_PARAM].getValue();
        lights[RATIO_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(ratioAttenValue));
        lights[RATIO_ATTEN_LEFT_LIGHT].setBrightness(ratioAttenValue < 0.0f ? -ratioAttenValue : 0.0f);
        lights[RATIO_ATTEN_RIGHT_LIGHT].setBrightness(ratioAttenValue > 0.0f ? ratioAttenValue : 0.0f);

        // Threshold CV attenuverter
        float thresholdAttenValue = params[THRESHOLD_ATTEN_PARAM].getValue();
        lights[THRESHOLD_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(thresholdAttenValue));
        lights[THRESHOLD_ATTEN_LEFT_LIGHT].setBrightness(thresholdAttenValue < 0.0f ? -thresholdAttenValue : 0.0f);
        lights[THRESHOLD_ATTEN_RIGHT_LIGHT].setBrightness(thresholdAttenValue > 0.0f ? thresholdAttenValue : 0.0f);

        // Release CV attenuverter
        float releaseAttenValue = params[RELEASE_ATTEN_PARAM].getValue();
        lights[RELEASE_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(releaseAttenValue));
        lights[RELEASE_ATTEN_LEFT_LIGHT].setBrightness(releaseAttenValue < 0.0f ? -releaseAttenValue : 0.0f);
        lights[RELEASE_ATTEN_RIGHT_LIGHT].setBrightness(releaseAttenValue > 0.0f ? releaseAttenValue : 0.0f);

        // Mix CV attenuverter
        float mixAttenValue = params[MIX_ATTEN_PARAM].getValue();
        lights[MIX_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(mixAttenValue));
        lights[MIX_ATTEN_LEFT_LIGHT].setBrightness(mixAttenValue < 0.0f ? -mixAttenValue : 0.0f);
        lights[MIX_ATTEN_RIGHT_LIGHT].setBrightness(mixAttenValue > 0.0f ? mixAttenValue : 0.0f);
    }
};

struct C1COMPCVWidget : ModuleWidget {
    C1COMPCVWidget(C1COMPCV* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/C1COMPCV.svg")));

        // No screws - magnetic faceplate

        // TC Logo (compact, same position as other 3HP expanders)
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::COMPACT, module);
        tcLogo->box.pos = Vec(22.5, 355);  // Centered for 3HP (45mm / 2 = 22.5)
        addChild(tcLogo);

        // Title label rendered by code below

        // CV Inputs and Attenuverters (vertical layout)
        // 3HP = 45mm width, center at X=22.5

        // Ratio CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 60), module, C1COMPCV::RATIO_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 103), module, C1COMPCV::RATIO_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 90.6), module, C1COMPCV::RATIO_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 111.8), module, C1COMPCV::RATIO_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 111.8), module, C1COMPCV::RATIO_ATTEN_RIGHT_LIGHT));

        // Threshold CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 130), module, C1COMPCV::THRESHOLD_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 173), module, C1COMPCV::THRESHOLD_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 160.6), module, C1COMPCV::THRESHOLD_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 181.8), module, C1COMPCV::THRESHOLD_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 181.8), module, C1COMPCV::THRESHOLD_ATTEN_RIGHT_LIGHT));

        // Release CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 200), module, C1COMPCV::RELEASE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 243), module, C1COMPCV::RELEASE_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 230.6), module, C1COMPCV::RELEASE_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 251.8), module, C1COMPCV::RELEASE_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 251.8), module, C1COMPCV::RELEASE_ATTEN_RIGHT_LIGHT));

        // Mix CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 270), module, C1COMPCV::MIX_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 313), module, C1COMPCV::MIX_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 300.6), module, C1COMPCV::MIX_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 321.8), module, C1COMPCV::MIX_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 321.8), module, C1COMPCV::MIX_ATTEN_RIGHT_LIGHT));
    }

    void draw(const DrawArgs& args) override {
        ModuleWidget::draw(args);

        // Draw title and labels (matching other modules' style)
        std::shared_ptr<Font> sonoBold = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
        std::shared_ptr<Font> sonoMedium = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));

        // Title (COM on first line, ·X· on second line) - Bold 18pt with black outline
        nvgFontFaceId(args.vg, sonoBold->handle);
        nvgFontSize(args.vg, 18.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        // Black outline for "COM" (Y=10 to match COMP title)
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 10 + dy * 0.5f, "COM", NULL);
                }
            }
        }
        // White fill for "COM"
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 10, "COM", NULL);

        // "·X·" with smooth color fade on dots
        float fade = module ? static_cast<C1COMPCV*>(module)->connectionFade : 0.0f;

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

        const char* labels[4] = {"RATIO", "THRES", "REL", "MIX"};
        float labelY[4] = {80, 150, 220, 290};

        for (int i = 0; i < 4; i++) {
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
    }
};

} // namespace

Model* modelC1COMPCV = createModel<C1COMPCV, C1COMPCVWidget>("C1COMPCV");
