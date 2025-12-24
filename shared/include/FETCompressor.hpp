#pragma once
#include "CompressorEngine.hpp"

// FET-style compressor (UREI 1176-inspired)
// Characteristics:
// - Ultra-fast attack (20µs to 800µs)
// - Aggressive, punchy character
// - Non-linear distortion/saturation
// - RMS detection
// - Adds harmonic coloration
class FETCompressor : public CompressorEngine {
public:
    FETCompressor();

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
    const char* getTypeName() const override { return "FET (1176)"; }

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
    bool autoReleaseMode;  // Not used in FET, but part of interface
    float kneeWidth;  // Knee width in dB (default: 0.0 = hard knee)

    // Detector state
    float gainReductionDb;
    float rmsState;  // RMS detector state

    // FET-specific parameters
    static constexpr float distortionAmount = 0.15f;  // Non-linear character amount
    static constexpr float rmsTimeConstant = 0.005f;  // 5ms RMS averaging

    // Helpers
    void recalculateCoefficients();
    float softClip(float x);  // Soft saturation curve
};
