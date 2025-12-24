#pragma once
#include "CompressorEngine.hpp"

// VCA-style compressor (SSL G-series bus compressor)
// Characteristics:
// - Clean, transparent compression
// - Fast attack (0.1-30ms)
// - Peak detection (not RMS)
// - Hard knee
// - Program-dependent AUTO release mode
class VCACompressor : public CompressorEngine {
public:
    VCACompressor();

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
    const char* getTypeName() const override { return "VCA (SSL G)"; }

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
    float kneeWidth;  // Knee width in dB (default: 0.0 = hard knee)

    // Detector state
    float gainReductionDb;

    // Helpers
    void recalculateCoefficients();
};
