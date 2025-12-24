#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include <cmath>

namespace {

// Forward declaration for message struct (matches ChanIn.cpp definition)
struct ChanInExpanderMessage {
    float levelCV;        // -1.0 to +1.0 (attenuated, represents ±12dB range)
    float hpfFreqCV;      // -5V to +5V (1V/oct for frequency control)
    float lpfFreqCV;      // -5V to +5V (1V/oct for frequency control)
    float phaseInvertCV;  // Gate: >1V = invert phase
};

// Dark PJ301M port
using DarkPJ301MPort = rack::componentlibrary::DarkPJ301MPort;

// Custom TinySimpleLight with 20% outer glow (halo)
template <typename TBase = rack::componentlibrary::YellowLight>
struct TinySimpleLightHalf : rack::componentlibrary::TinySimpleLight<TBase> {
    TinySimpleLightHalf() {
        this->bgColor = nvgRGBA(0x33, 0x33, 0x33, 0x33); // 20% alpha (was 0xff)
        this->borderColor = nvgRGBA(0, 0, 0, 11);        // 20% alpha (was 53)
    }
};

struct ChanInCV : Module {
    enum ParamIds {
        LEVEL_ATTEN_PARAM,    // Level CV attenuverter (-1 to +1)
        HPF_ATTEN_PARAM,      // HPF CV attenuverter (-1 to +1)
        LPF_ATTEN_PARAM,      // LPF CV attenuverter (-1 to +1)
        PARAMS_LEN
    };

    enum InputIds {
        LEVEL_CV_INPUT,
        HPF_CV_INPUT,
        LPF_CV_INPUT,
        PHASE_CV_INPUT,
        INPUTS_LEN
    };

    enum LightIds {
        LEVEL_ATTEN_TOP_LIGHT,
        LEVEL_ATTEN_LEFT_LIGHT,
        LEVEL_ATTEN_RIGHT_LIGHT,
        HPF_ATTEN_TOP_LIGHT,
        HPF_ATTEN_LEFT_LIGHT,
        HPF_ATTEN_RIGHT_LIGHT,
        LPF_ATTEN_TOP_LIGHT,
        LPF_ATTEN_LEFT_LIGHT,
        LPF_ATTEN_RIGHT_LIGHT,
        LIGHTS_LEN
    };

    // Message buffers for communication with CHAN-IN
    ChanInExpanderMessage leftMessages[2];  // Double buffer for thread safety

    // CV smoothing filters (1ms time constant to prevent zipper noise)
    dsp::TExponentialFilter<float> levelCVFilter;
    dsp::TExponentialFilter<float> hpfCVFilter;
    dsp::TExponentialFilter<float> lpfCVFilter;

    // Connection indicator fade (0.0 = white, 1.0 = amber)
    float connectionFade = 0.0f;

    ChanInCV() {
        config(PARAMS_LEN, INPUTS_LEN, 0, LIGHTS_LEN);

        // Configure attenuverter parameters (bipolar -1 to +1, default 0%)
        configParam(LEVEL_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Level CV Amount", "%", 0.0f, 100.0f);
        configParam(HPF_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "HPF CV Amount", "%", 0.0f, 100.0f);
        configParam(LPF_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "LPF CV Amount", "%", 0.0f, 100.0f);

        // Configure CV inputs
        configInput(LEVEL_CV_INPUT, "Level CV");
        configInput(HPF_CV_INPUT, "HPF Frequency CV");
        configInput(LPF_CV_INPUT, "LPF Frequency CV");
        configInput(PHASE_CV_INPUT, "Phase Invert CV");

        // Initialize expander messages
        leftExpander.producerMessage = &leftMessages[0];
        leftExpander.consumerMessage = &leftMessages[1];

        // Initialize CV smoothing filters (~1ms time constant)
        levelCVFilter.setLambda(1000.0f);
        hpfCVFilter.setLambda(1000.0f);
        lpfCVFilter.setLambda(1000.0f);
    }

    void process(const ProcessArgs& args) override {
        // Check if CHAN-IN is connected to the left
        if (leftExpander.module && leftExpander.module->model == modelChanIn) {
            // VCV Rack automatically points leftExpander.producerMessage to CHAN-IN's consumerMessage
            ChanInExpanderMessage* msg = (ChanInExpanderMessage*)leftExpander.producerMessage;

            // Read CV inputs with smoothing and attenuverters
            // Level CV: ±10V input → -1 to +1 (attenuated)
            if (inputs[LEVEL_CV_INPUT].isConnected()) {
                float raw = inputs[LEVEL_CV_INPUT].getVoltage();
                float smoothed = levelCVFilter.process(args.sampleTime, raw);
                msg->levelCV = (smoothed / 10.0f) * params[LEVEL_ATTEN_PARAM].getValue();
            } else {
                msg->levelCV = 0.0f;
            }

            // HPF Frequency CV: ±5V (1V/oct)
            if (inputs[HPF_CV_INPUT].isConnected()) {
                float raw = inputs[HPF_CV_INPUT].getVoltage();
                float smoothed = hpfCVFilter.process(args.sampleTime, raw);
                msg->hpfFreqCV = smoothed * params[HPF_ATTEN_PARAM].getValue();
            } else {
                msg->hpfFreqCV = 0.0f;
            }

            // LPF Frequency CV: ±5V (1V/oct)
            if (inputs[LPF_CV_INPUT].isConnected()) {
                float raw = inputs[LPF_CV_INPUT].getVoltage();
                float smoothed = lpfCVFilter.process(args.sampleTime, raw);
                msg->lpfFreqCV = smoothed * params[LPF_ATTEN_PARAM].getValue();
            } else {
                msg->lpfFreqCV = 0.0f;
            }

            // Phase Invert CV: Gate (>1V = invert)
            if (inputs[PHASE_CV_INPUT].isConnected()) {
                msg->phaseInvertCV = inputs[PHASE_CV_INPUT].getVoltage();
            } else {
                msg->phaseInvertCV = 0.0f;
            }

            // Flip message buffers
            leftExpander.messageFlipRequested = true;
        }

        // Smooth fade for connection indicator (200ms fade time)
        bool isConnected = leftExpander.module && leftExpander.module->model == modelChanIn;
        float targetFade = isConnected ? 1.0f : 0.0f;
        float fadeSpeed = 5.0f;  // 5 units/sec = 200ms fade
        connectionFade += (targetFade - connectionFade) * fadeSpeed * args.sampleTime;

        // Update attenuverter position lights (smooth fade between positions)

        // Level CV attenuverter
        float levelAttenValue = params[LEVEL_ATTEN_PARAM].getValue();
        lights[LEVEL_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(levelAttenValue));
        lights[LEVEL_ATTEN_LEFT_LIGHT].setBrightness(levelAttenValue < 0.0f ? -levelAttenValue : 0.0f);
        lights[LEVEL_ATTEN_RIGHT_LIGHT].setBrightness(levelAttenValue > 0.0f ? levelAttenValue : 0.0f);

        // HPF CV attenuverter
        float hpfAttenValue = params[HPF_ATTEN_PARAM].getValue();
        lights[HPF_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(hpfAttenValue));
        lights[HPF_ATTEN_LEFT_LIGHT].setBrightness(hpfAttenValue < 0.0f ? -hpfAttenValue : 0.0f);
        lights[HPF_ATTEN_RIGHT_LIGHT].setBrightness(hpfAttenValue > 0.0f ? hpfAttenValue : 0.0f);

        // LPF CV attenuverter
        float lpfAttenValue = params[LPF_ATTEN_PARAM].getValue();
        lights[LPF_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(lpfAttenValue));
        lights[LPF_ATTEN_LEFT_LIGHT].setBrightness(lpfAttenValue < 0.0f ? -lpfAttenValue : 0.0f);
        lights[LPF_ATTEN_RIGHT_LIGHT].setBrightness(lpfAttenValue > 0.0f ? lpfAttenValue : 0.0f);
    }
};

struct ChanInCVWidget : ModuleWidget {
    ChanInCVWidget(ChanInCV* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ChanInCV.svg")));

        // No screws - magnetic faceplate

        // TC Logo (compact, same position as C1 module)
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::COMPACT, module);
        tcLogo->box.pos = Vec(22.5, 355);  // Centered for 3HP (45mm / 2 = 22.5)
        addChild(tcLogo);

        // Title label rendered by code below

        // CV Inputs and Attenuverters (vertical layout)
        // 3HP = 45mm width, center at X=22.5

        // Level CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 60), module, ChanInCV::LEVEL_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 103), module, ChanInCV::LEVEL_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 90.6), module, ChanInCV::LEVEL_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 111.8), module, ChanInCV::LEVEL_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 111.8), module, ChanInCV::LEVEL_ATTEN_RIGHT_LIGHT));

        // HPF CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 130), module, ChanInCV::HPF_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 173), module, ChanInCV::HPF_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 160.6), module, ChanInCV::HPF_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 181.8), module, ChanInCV::HPF_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 181.8), module, ChanInCV::HPF_ATTEN_RIGHT_LIGHT));

        // LPF CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 200), module, ChanInCV::LPF_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 243), module, ChanInCV::LPF_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 230.6), module, ChanInCV::LPF_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 251.8), module, ChanInCV::LPF_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 251.8), module, ChanInCV::LPF_ATTEN_RIGHT_LIGHT));

        // Phase CV input (no attenuverter - simple gate)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 270), module, ChanInCV::PHASE_CV_INPUT));
    }

    void draw(const DrawArgs& args) override {
        ModuleWidget::draw(args);

        // Draw title and labels (matching other modules' style)
        std::shared_ptr<Font> sonoBold = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
        std::shared_ptr<Font> sonoMedium = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));

        // Title (CHI on first line, ·X· on second line) - Bold 18pt with black outline
        nvgFontFaceId(args.vg, sonoBold->handle);
        nvgFontSize(args.vg, 18.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        // Black outline for "CHI" (Y=10 to match parent module title)
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 10 + dy * 0.5f, "CHI", NULL);
                }
            }
        }
        // White fill for "CHI"
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 10, "CHI", NULL);

        // "·X·" with smooth color fade on dots
        float fade = module ? static_cast<ChanInCV*>(module)->connectionFade : 0.0f;

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

        const char* labels[4] = {"GAIN", "HPF", "LPF", "PHASE"};
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

Model* modelChanInCV = createModel<ChanInCV, ChanInCVWidget>("ChanInCV");
