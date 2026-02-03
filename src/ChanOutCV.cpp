#include "plugin.hpp"
#include "../shared/include/TCLogo.hpp"
#include <cmath>

namespace {

// Forward declaration for message struct (matches ChanOut.cpp definition)
struct ChanOutExpanderMessage {
    float gainCV;      // -1.0 to +1.0 (attenuated)
    float panCV;       // -1.0 to +1.0 (attenuated)
    float driveCV;     // -1.0 to +1.0 (attenuated)
    float characterCV; // -1.0 to +1.0 (attenuated)
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

struct ChanOutCV : Module {
    enum ParamIds {
        GAIN_ATTEN_PARAM,      // Gain CV attenuverter (-1 to +1)
        PAN_ATTEN_PARAM,       // Pan CV attenuverter (-1 to +1)
        DRIVE_ATTEN_PARAM,     // Drive CV attenuverter (-1 to +1)
        CHAR_ATTEN_PARAM,      // Character CV attenuverter (-1 to +1)
        PARAMS_LEN
    };

    enum InputIds {
        GAIN_CV_INPUT,
        PAN_CV_INPUT,
        DRIVE_CV_INPUT,
        CHAR_CV_INPUT,
        INPUTS_LEN
    };

    enum LightIds {
        GAIN_ATTEN_TOP_LIGHT,
        GAIN_ATTEN_LEFT_LIGHT,
        GAIN_ATTEN_RIGHT_LIGHT,
        PAN_ATTEN_TOP_LIGHT,
        PAN_ATTEN_LEFT_LIGHT,
        PAN_ATTEN_RIGHT_LIGHT,
        DRIVE_ATTEN_TOP_LIGHT,
        DRIVE_ATTEN_LEFT_LIGHT,
        DRIVE_ATTEN_RIGHT_LIGHT,
        CHAR_ATTEN_TOP_LIGHT,
        CHAR_ATTEN_LEFT_LIGHT,
        CHAR_ATTEN_RIGHT_LIGHT,
        LIGHTS_LEN
    };

    // Message buffers for communication with CHAN-OUT
    ChanOutExpanderMessage leftMessages[2] = {};  // Double buffer for thread safety

    // CV smoothing filters (1ms time constant to prevent zipper noise)
    dsp::TExponentialFilter<float> gainCVFilter;
    dsp::TExponentialFilter<float> panCVFilter;
    dsp::TExponentialFilter<float> driveCVFilter;
    dsp::TExponentialFilter<float> charCVFilter;

    // Connection indicator fade (0.0 = white, 1.0 = amber)
    float connectionFade = 0.0f;

    ChanOutCV() {
        config(PARAMS_LEN, INPUTS_LEN, 0, LIGHTS_LEN);

        // Configure attenuverter parameters (bipolar -1 to +1, default 0%)
        configParam(GAIN_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Gain CV Amount", "%", 0.0f, 100.0f);
        configParam(PAN_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Pan CV Amount", "%", 0.0f, 100.0f);
        configParam(DRIVE_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Drive CV Amount", "%", 0.0f, 100.0f);
        configParam(CHAR_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Character CV Amount", "%", 0.0f, 100.0f);

        // Configure CV inputs
        configInput(GAIN_CV_INPUT, "Gain CV");
        configInput(PAN_CV_INPUT, "Pan CV");
        configInput(DRIVE_CV_INPUT, "Drive CV");
        configInput(CHAR_CV_INPUT, "Character CV");

        // Initialize expander messages
        leftExpander.producerMessage = &leftMessages[0];
        leftExpander.consumerMessage = &leftMessages[1];

        // Initialize CV smoothing filters (~1ms time constant)
        gainCVFilter.setLambda(1000.0f);
        panCVFilter.setLambda(1000.0f);
        driveCVFilter.setLambda(1000.0f);
        charCVFilter.setLambda(1000.0f);
    }

    void process(const ProcessArgs& args) override {
        // Check if CHAN-OUT is connected to the left
        if (leftExpander.module && leftExpander.module->model == modelChanOut) {
            // VCV Rack automatically points leftExpander.producerMessage to CHAN-OUT's consumerMessage
            ChanOutExpanderMessage* msg = (ChanOutExpanderMessage*)leftExpander.producerMessage;

            // Read CV inputs with smoothing and attenuverters
            // ±10V input → -1 to +1 (attenuated)
            if (inputs[GAIN_CV_INPUT].isConnected()) {
                float raw = inputs[GAIN_CV_INPUT].getVoltage();
                float smoothed = gainCVFilter.process(args.sampleTime, raw);
                msg->gainCV = (smoothed / 10.0f) * params[GAIN_ATTEN_PARAM].getValue();
            } else {
                msg->gainCV = 0.0f;
            }

            if (inputs[PAN_CV_INPUT].isConnected()) {
                float raw = inputs[PAN_CV_INPUT].getVoltage();
                float smoothed = panCVFilter.process(args.sampleTime, raw);
                msg->panCV = (smoothed / 10.0f) * params[PAN_ATTEN_PARAM].getValue();
            } else {
                msg->panCV = 0.0f;
            }

            if (inputs[DRIVE_CV_INPUT].isConnected()) {
                float raw = inputs[DRIVE_CV_INPUT].getVoltage();
                float smoothed = driveCVFilter.process(args.sampleTime, raw);
                msg->driveCV = (smoothed / 10.0f) * params[DRIVE_ATTEN_PARAM].getValue();
            } else {
                msg->driveCV = 0.0f;
            }

            if (inputs[CHAR_CV_INPUT].isConnected()) {
                float raw = inputs[CHAR_CV_INPUT].getVoltage();
                float smoothed = charCVFilter.process(args.sampleTime, raw);
                msg->characterCV = (smoothed / 10.0f) * params[CHAR_ATTEN_PARAM].getValue();
            } else {
                msg->characterCV = 0.0f;
            }

            // Flip message buffers
            leftExpander.messageFlipRequested = true;
        }

        // Smooth fade for connection indicator (200ms fade time)
        bool isConnected = leftExpander.module && leftExpander.module->model == modelChanOut;
        float targetFade = isConnected ? 1.0f : 0.0f;
        float fadeSpeed = 5.0f;  // 5 units/sec = 200ms fade
        connectionFade += (targetFade - connectionFade) * fadeSpeed * args.sampleTime;

        // Update attenuverter position lights (smooth fade between positions)

        // Gain CV attenuverter
        float gainAttenValue = params[GAIN_ATTEN_PARAM].getValue();
        lights[GAIN_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(gainAttenValue));
        lights[GAIN_ATTEN_LEFT_LIGHT].setBrightness(gainAttenValue < 0.0f ? -gainAttenValue : 0.0f);
        lights[GAIN_ATTEN_RIGHT_LIGHT].setBrightness(gainAttenValue > 0.0f ? gainAttenValue : 0.0f);

        // Pan CV attenuverter
        float panAttenValue = params[PAN_ATTEN_PARAM].getValue();
        lights[PAN_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(panAttenValue));
        lights[PAN_ATTEN_LEFT_LIGHT].setBrightness(panAttenValue < 0.0f ? -panAttenValue : 0.0f);
        lights[PAN_ATTEN_RIGHT_LIGHT].setBrightness(panAttenValue > 0.0f ? panAttenValue : 0.0f);

        // Drive CV attenuverter
        float driveAttenValue = params[DRIVE_ATTEN_PARAM].getValue();
        lights[DRIVE_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(driveAttenValue));
        lights[DRIVE_ATTEN_LEFT_LIGHT].setBrightness(driveAttenValue < 0.0f ? -driveAttenValue : 0.0f);
        lights[DRIVE_ATTEN_RIGHT_LIGHT].setBrightness(driveAttenValue > 0.0f ? driveAttenValue : 0.0f);

        // Character CV attenuverter
        float charAttenValue = params[CHAR_ATTEN_PARAM].getValue();
        lights[CHAR_ATTEN_TOP_LIGHT].setBrightness(1.0f - std::abs(charAttenValue));
        lights[CHAR_ATTEN_LEFT_LIGHT].setBrightness(charAttenValue < 0.0f ? -charAttenValue : 0.0f);
        lights[CHAR_ATTEN_RIGHT_LIGHT].setBrightness(charAttenValue > 0.0f ? charAttenValue : 0.0f);
    }
};

struct ChanOutCVWidget : ModuleWidget {
    ChanOutCVWidget(ChanOutCV* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ChanOutCV.svg")));

        // No screws - magnetic faceplate

        // TC Logo (compact, same position as other 3HP expanders)
        TCLogoWidget* tcLogo = new TCLogoWidget(TCLogoWidget::COMPACT, module);
        tcLogo->box.pos = Vec(22.5, 355);  // Centered for 3HP (45mm / 2 = 22.5)
        addChild(tcLogo);

        // Title label rendered by code below

        // CV Inputs and Attenuverters (vertical layout)
        // 3HP = 45mm width, center at X=22.5

        // Gain CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 60), module, ChanOutCV::GAIN_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 103), module, ChanOutCV::GAIN_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 90.6), module, ChanOutCV::GAIN_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 111.8), module, ChanOutCV::GAIN_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 111.8), module, ChanOutCV::GAIN_ATTEN_RIGHT_LIGHT));

        // Pan CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 130), module, ChanOutCV::PAN_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 173), module, ChanOutCV::PAN_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 160.6), module, ChanOutCV::PAN_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 181.8), module, ChanOutCV::PAN_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 181.8), module, ChanOutCV::PAN_ATTEN_RIGHT_LIGHT));

        // Drive CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 200), module, ChanOutCV::DRIVE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 243), module, ChanOutCV::DRIVE_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 230.6), module, ChanOutCV::DRIVE_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 251.8), module, ChanOutCV::DRIVE_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 251.8), module, ChanOutCV::DRIVE_ATTEN_RIGHT_LIGHT));

        // Character CV section
        addInput(createInputCentered<DarkPJ301MPort>(Vec(22.5, 270), module, ChanOutCV::CHAR_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(22.5, 313), module, ChanOutCV::CHAR_ATTEN_PARAM));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(22.5, 300.6), module, ChanOutCV::CHAR_ATTEN_TOP_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(14.3, 321.8), module, ChanOutCV::CHAR_ATTEN_LEFT_LIGHT));
        addChild(createLightCentered<TinySimpleLightHalf<YellowLight>>(Vec(30.7, 321.8), module, ChanOutCV::CHAR_ATTEN_RIGHT_LIGHT));
    }

    void draw(const DrawArgs& args) override {
        ModuleWidget::draw(args);

        // Draw title and labels (matching other modules' style)
        std::shared_ptr<Font> sonoBold = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Bold.ttf"));
        std::shared_ptr<Font> sonoMedium = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sono/static/Sono_Proportional-Medium.ttf"));

        // Title (CHO on first line, ·X· on second line) - Bold 18pt with black outline
        nvgFontFaceId(args.vg, sonoBold->handle);
        nvgFontSize(args.vg, 18.0f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        // Black outline for "CHO" (Y=10 to match parent module title)
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 || dy != 0) {
                    nvgText(args.vg, 22.5 + dx * 0.5f, 10 + dy * 0.5f, "CHO", NULL);
                }
            }
        }
        // White fill for "CHO"
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 22.5, 10, "CHO", NULL);

        // "·X·" with smooth color fade on dots
        float fade = module ? static_cast<ChanOutCV*>(module)->connectionFade : 0.0f;

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

        const char* labels[4] = {"GAIN", "PAN", "DRIVE", "CHAR"};
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

Model* modelChanOutCV = createModel<ChanOutCV, ChanOutCVWidget>("ChanOutCV");
