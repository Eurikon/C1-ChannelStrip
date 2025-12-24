#pragma once
#include <cmath>
#include <algorithm>

// Base class for all compressor engine types
// Each compressor type (VCA, FET, Optical, Vari-Mu) inherits from this interface
class CompressorEngine {
public:
    virtual ~CompressorEngine() {}

    // Core DSP methods (must be implemented by derived classes)
    virtual void setSampleRate(float sr) = 0;
    virtual void setThreshold(float db) = 0;
    virtual void setRatio(float r) = 0;
    virtual void setAttack(float ms) = 0;
    virtual void setRelease(float ms) = 0;
    virtual void setMakeup(float db) = 0;
    virtual void setAutoRelease(bool enable) = 0;
    virtual void setKnee(float db) = 0;  // Set knee width override (-1 = use engine default)

    // Process stereo audio
    virtual void processStereo(float inL, float inR, float* outL, float* outR) = 0;

    // Process stereo audio with external sidechain key signal
    // keyLevel: absolute level of sidechain signal (0.0 to 10.0 typical VCV Rack range)
    virtual void processStereoWithKey(float inL, float inR, float keyLevel, float* outL, float* outR) = 0;

    // Get current gain reduction in dB (negative value: 0 to -20dB)
    virtual float getGainReduction() const = 0;

    // Get compressor type name for display
    virtual const char* getTypeName() const = 0;

protected:
    // Utility functions available to all compressor types
    float dbToLin(float db) const { return std::pow(10.0f, db / 20.0f); }
    float linToDb(float lin) const { return 20.0f * std::log10(std::max(lin, 1e-12f)); }
};
