#pragma once
#include "CompressorEngine.hpp"

// Vari-Mu (Variable-Mu) tube compressor (Fairchild 670-inspired)
// Characteristics:
// - Slowest attack/release of all types
// - Very smooth, musical compression
// - Tube saturation and harmonics
// - RMS detection
// - Extremely transparent at low ratios
// - "Glue" and "warmth" character
class VariMuCompressor : public CompressorEngine {
public:
    VariMuCompressor();

    // CompressorEngine interface implementation
    void setSampleRate(float sr) override;
    void setThreshold(float db) override;
    void setRatio(float r) override;
    void setAttack(float ms) override;
    void setRelease(float ms) override;
    void setMakeup(float db) override;
    void setAutoRelease(bool enable) override;
    void setKnee(float db) override;

    void processStereo(float inL, float inR, float* outL, float* outR) override;
    void processStereoWithKey(float inL, float inR, float keyLevel, float* outL, float* outR) override;
    float getGainReduction() const override { return -gainReductionDb; }
    const char* getTypeName() const override { return "Vari-Mu (Fairchild)"; }

private:
    // Parameters
    float sampleRate;
    float thresholdDb;
    float ratio;
    float attackMs;
    float releaseMs;
    float attackCoeff;
    float releaseCoeff;
    float makeupGain;
    bool autoReleaseMode;
    float kneeWidth;  // Knee width in dB (default: 12.0 = extra-soft knee)

    // Detector state
    float gainReductionDb;
    float rmsState;
    float tubeStateL;  // Tube grid state for left channel
    float tubeStateR;  // Tube grid state for right channel

    // Vari-Mu specific parameters
    static constexpr float rmsTimeConstant = 0.020f;  // 20ms RMS averaging (slowest)
    static constexpr float tubeSaturation = 0.25f;  // Tube harmonic amount
    static constexpr float tubeAsymmetry = 0.1f;  // Even harmonic asymmetry

    // Helpers
    void recalculateCoefficients();
    float tubeSaturate(float x, float& tubeState);  // Tube saturation curve with memory
};
