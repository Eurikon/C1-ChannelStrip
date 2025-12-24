#pragma once
#include "CompressorEngine.hpp"

// Optical-style compressor (LA-2A-inspired)
// Characteristics:
// - Slow, smooth attack (10ms+)
// - Program-dependent release curve
// - RMS detection
// - Very musical, transparent compression
// - Release time varies with signal level (opto-resistor behavior)
class OpticalCompressor : public CompressorEngine {
public:
    OpticalCompressor();

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
    const char* getTypeName() const override { return "Optical (LA-2A)"; }

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
    float kneeWidth;  // Knee width in dB (default: 6.0 = soft knee)

    // Detector state
    float gainReductionDb;
    float rmsState;
    float optoState;  // Opto-resistor state (simulates light-dependent resistor)

    // Optical-specific parameters
    static constexpr float rmsTimeConstant = 0.010f;  // 10ms RMS averaging (slower than FET)
    static constexpr float optoDecay = 0.95f;  // Slow opto decay characteristic

    // Helpers
    void recalculateCoefficients();
    float calculateOptoRelease(float grLevel);  // Time-varying release based on GR
};
