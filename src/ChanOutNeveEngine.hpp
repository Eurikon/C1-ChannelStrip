//***********************************************************************************************
// ChanOut Neve 8816 Engine v3 - PARALLEL BLEND Architecture
// For C1-ChannelStrip VCV Rack Plugin
//
// COMPLETE REDESIGN v3 - October 2025
// Inspired by Dangerous 2-BUS+ parallel blend architecture
//
// Key improvements:
// - PARALLEL BLEND: Clean signal mixed with colored signal (like DM2+)
// - Unity-gain tanh with division for amplitude preservation
// - Simplified transformer core (no threshold complexity)
// - Character controls blend amount, not processing parameters
// - Always transparent at neutral, musical with character engaged
//
// Based on Neve 80-series topology with transformer-coupled outputs:
// - 8× polyphase oversampling (SIMD-optimized)
// - Transformer hysteresis modeling via flux state (leaky integrator)
// - Silk Red/Blue character via pre/de-emphasis filtering
// - Zener-style asymmetric knee for odd-order content
//
// Hardware Reference (informational only, not circuit emulation):
// - Neve 8816 summing mixer: THD < 0.02% @ +20 dBu
// - Carnhill/Marinair transformer behavior (level-dependent saturation)
//
// License: GPL-3.0-or-later
//
// IMPORTANT: This is an approximation inspired by published specifications,
// not a circuit-accurate emulation.
//***********************************************************************************************

#pragma once

#include "rack.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

#if defined(__SSE2__)
#include <emmintrin.h>
#define HAS_SSE2 1
#else
#define HAS_SSE2 0
#endif

using namespace rack;

namespace ChanOutNeve {

static inline double clampd(double x, double a, double b) {
    return (x < a) ? a : (x > b) ? b : x;
}

// Buffered Polyphase Oversampler with SIMD-optimized inner loop
class BufferedPolyphaseSIMD {
public:
    BufferedPolyphaseSIMD(int factor = 8, int tapsPerPhase = 64)
    : factor_(std::max(1, factor)), tapsPerPhase_(std::max(8, tapsPerPhase)) {
        buildKernel();
        setFactor(factor_);
        ring_.assign(tapsPerPhase_ + 8, 0.0);
        writeIdx_ = 0;
    }

    void setFactor(int f) {
        factor_ = std::max(1, f);
        buildPolyphase();
    }

    int factor() const { return factor_; }
    void reset() { std::fill(ring_.begin(), ring_.end(), 0.0); writeIdx_ = 0; }

    // ProcessUp: SIMD-accelerated inner convolution
    void processUp(const double* in, size_t n, double* out) {
        if (factor_ == 1) {
            for (size_t i = 0; i < n; ++i) out[i] = in[i];
            return;
        }
        const int P = tapsPerPhase_;
        const int F = factor_;
        const int ringSize = (int)ring_.size();

        for (size_t i = 0; i < n; ++i) {
            ring_[writeIdx_] = in[i];
            int base = writeIdx_ - (P - 1);
            if (base < 0) base += ringSize;

            for (int ph = 0; ph < F; ++ph) {
                const double* taps = &polyTaps_[ph * P];
                double s = 0.0;
#if HAS_SSE2
                // SIMD path: process two doubles per iteration using __m128d
                __m128d acc = _mm_setzero_pd();
                int idx = base;
                int k = 0;
                for (; k + 1 < P; k += 2) {
                    // load ring[idx] and ring[idx+1] (handle wrap)
                    double a0 = ring_[idx];
                    int idx1 = idx + 1; if (idx1 >= ringSize) idx1 -= ringSize;
                    double a1 = ring_[idx1];
                    __m128d va = _mm_set_pd(a1, a0);

                    // load taps[k], taps[k+1]
                    __m128d vt = _mm_loadu_pd(&taps[k]);
                    acc = _mm_add_pd(acc, _mm_mul_pd(va, vt));

                    idx += 2; if (idx >= ringSize) idx -= ringSize;
                }
                // horizontal add acc
                double tmp[2]; _mm_storeu_pd(tmp, acc);
                s = tmp[0] + tmp[1];
                // tail
                for (; k < P; ++k) { s += ring_[idx] * taps[k]; idx++; if (idx >= ringSize) idx -= ringSize; }
#else
                // scalar fallback
                int idx = base;
                for (int k = 0; k < P; ++k) { s += ring_[idx] * taps[k]; idx++; if (idx >= ringSize) idx -= ringSize; }
#endif
                out[i*F + ph] = s;
            }
            writeIdx_++; if (writeIdx_ >= ringSize) writeIdx_ = 0;
        }
    }

    void processDown(const double* in, size_t inLen, double* out) {
        if (factor_ == 1) { for (size_t i = 0; i < inLen; ++i) out[i] = in[i]; return; }
        // Proper decimation: keep every Nth sample (phase 0 only)
        // The anti-aliasing was already applied during processUp
        size_t outN = inLen / factor_;
        for (size_t i = 0; i < outN; ++i) {
            out[i] = in[i * factor_];  // Take phase 0 sample only
        }
    }

private:
    int factor_ = 8;
    int tapsPerPhase_ = 64;
    std::vector<double> kernel_;
    std::vector<double> polyTaps_;
    std::vector<double> ring_;
    int writeIdx_ = 0;

    void buildKernel() {
        int N = tapsPerPhase_ * factor_;
        kernel_.assign(N, 0.0);
        double fc = 0.45 / double(factor_);
        double M = double(N) - 1.0;
        for (int n = 0; n < N; ++n) {
            double x = double(n) - M/2.0;
            double sinc = (x == 0.0) ? 2.0*fc : std::sin(2.0*M_PI*fc*x)/(M_PI*x);
            double w = 0.42 - 0.5*std::cos(2.0*M_PI*n/M) + 0.08*std::cos(4.0*M_PI*n/M);
            kernel_[n] = sinc * w;
        }
        // Normalize: for polyphase upsampling, sum of taps must equal factor_
        double sum = 0.0;
        for (int n = 0; n < N; ++n) sum += kernel_[n];
        if (sum > 1e-12) {
            double scale = double(factor_) / sum;
            for (int n = 0; n < N; ++n) kernel_[n] *= scale;
        }
    }

    void buildPolyphase() {
        int N = tapsPerPhase_ * factor_;
        if ((int)kernel_.size() != N) buildKernel();
        polyTaps_.assign(factor_ * tapsPerPhase_, 0.0);
        for (int ph = 0; ph < factor_; ++ph) {
            for (int k = 0; k < tapsPerPhase_; ++k) {
                int idx = ph + k * factor_;
                if (idx < (int)kernel_.size()) polyTaps_[ph * tapsPerPhase_ + k] = kernel_[idx];
                else polyTaps_[ph * tapsPerPhase_ + k] = 0.0;
            }
        }
    }
};

// Neve Transformer Core v3 - Simple unity-gain tanh (like DM2+)
class NeveTransformerCore_v3 {
public:
    void setSampleRate(double fs) {
        fs_ = (fs > 1.0 ? fs : 44100.0);
        setFluxTimeConstantMs(10.0);
    }

    void reset() { flux_ = 0.0; }

    void setFluxTimeConstantMs(double ms) {
        double seconds = std::max(0.1, ms) * 1e-3;
        double a = std::exp(-1.0 / (seconds * fs_));
        alpha_ = clampd(a, 0.0, 0.999999);
        beta_ = 1.0 - alpha_;
    }

    void setFluxBiasGain(double g) { biasGain_ = clampd(g, 0.0, 0.12); }
    void setZenerDrive(double z) { zenerDrive_ = clampd(z, 0.0, 1.0); }

    // Simple processing - always runs (blend controls audibility)
    inline double process(double x) {
        // Flux integration (always running, like DM2+)
        flux_ = alpha_ * flux_ + beta_ * x;

        // Flux bias
        double bias = biasGain_ * std::tanh(flux_ * 2.0);
        double u = x + bias;

        // Unity-gain tanh saturation (LIKE DM2+ TRANSFORMER)
        // This preserves amplitude while adding harmonics
        double saturated = std::tanh(satK_ * u) / std::tanh(satK_);

        // Asymmetric knee for Red mode (optional)
        double y = zenerKnee(saturated);

        return clampd(y, -1.2, 1.2);
    }

private:
    // Asymmetric knee for odd-order content (zener-style)
    inline double zenerKnee(double x) {
        if (zenerDrive_ < 1e-6) return x;

        const double kneeThreshold = 0.90;

        if (x > kneeThreshold) {
            double over = x - kneeThreshold;
            double posK = 1.0 + 4.0 * zenerDrive_;
            return kneeThreshold + std::atan(over * posK) / posK;
        } else if (x < -kneeThreshold) {
            double over = x + kneeThreshold;
            double negK = 1.0 + 2.0 * zenerDrive_;
            return -kneeThreshold + std::atan(over * negK) / negK;
        }
        return x;
    }

    double fs_ = 44100.0;
    double flux_ = 0.0;
    double alpha_ = 0.995;
    double beta_ = 0.005;

    // Fixed parameters
    const double satK_ = 1.5;  // Fixed saturation steepness
    double biasGain_ = 0.03;   // Flux bias amount
    double zenerDrive_ = 0.0;  // Asymmetry amount
};

// Neve 8816 Engine Core v3 - PARALLEL BLEND
class NEVE8816_Engine_Pro_SIMD_v3 {
public:
    NEVE8816_Engine_Pro_SIMD_v3(double sampleRate = 44100.0, int oversampleFactor = 8)
    : oversampler_(oversampleFactor, 64), oversampleFactor_(oversampleFactor) {
        fs_ = sampleRate;
        init();
    }

    void init() {
        oversampler_.reset();
        dcState_ = 0.0;
        lpRedState_ = 0.0;
        lpBlueState_ = 0.0;

        // Pre-allocate upsample buffer (max: 32 samples × 8× oversampling = 256)
        upsampleBuffer_.resize(256);

        // Silk filter frequencies
        redHz_ = 3500.0;
        blueHz_ = 140.0;
        updateAlphas();

        drive_ = 1.0;
        character_ = 0.0;  // -1.0 = Blue, 0.0 = Neutral, +1.0 = Red

        core_.setSampleRate(fs_);
        core_.setFluxTimeConstantMs(10.0);
        updateCoreFromCharacter();
    }

    void setSampleRate(double fs) {
        fs_ = (fs > 1.0 ? fs : fs_);
        updateAlphas();
        core_.setSampleRate(fs_);
    }

    void setOversampleFactor(int f) {
        oversampleFactor_ = std::max(1, f);
        oversampler_.setFactor(oversampleFactor_);
    }

    void setDriveDb(double db) {
        drive_ = std::pow(10.0, db/20.0);
    }

    void setCharacter(double c) {
        character_ = clampd(c, -1.0, 1.0);
        updateCoreFromCharacter();
    }

    void reset() {
        oversampler_.reset();
        dcState_ = 0.0;
        lpRedState_ = 0.0;
        lpBlueState_ = 0.0;
        core_.reset();
    }

    void processBlock(const double* in, double* out, size_t N) {
        if (oversampleFactor_ == 1) {
            for (size_t i = 0; i < N; ++i)
                out[i] = processSampleInternal(in[i]);
            return;
        }

        size_t M = N * oversampleFactor_;
        // Use pre-allocated buffer (no per-sample allocation)
        oversampler_.processUp(in, N, upsampleBuffer_.data());

        for (size_t i = 0; i < M; ++i) {
            double s = upsampleBuffer_[i] * drive_;

            // DC blocker (very gentle)
            s -= dcState_ * 1e-4;
            dcState_ = 0.9999 * dcState_ + 1e-4 * s;

            // PARALLEL BLEND ARCHITECTURE (like DM2+)
            // Character controls blend amount between clean and colored
            double colorAmt = std::abs(character_) * 0.85;  // 0 to 0.85 max

            // Clean path: untouched signal
            double clean = s;

            // Colored path: Neve character processing
            // Silk pre-emphasis filters
            lpRedState_ += redAlpha_ * (s - lpRedState_);
            double hpRed = s - lpRedState_;
            lpBlueState_ += blueAlpha_ * (s - lpBlueState_);
            double lpBlue = lpBlueState_;

            // Character amounts (0.0 to 1.0 each)
            double red  = std::max(0.0,  character_);
            double blue = std::max(0.0, -character_);

            // Pre-emphasis: boost frequency-selective content before saturation
            double preGain = 0.5;
            double pre = s + preGain * (red * hpRed + blue * lpBlue);

            // Transformer saturation
            double saturated = core_.process(pre);

            // De-emphasis: remove ORIGINAL boost (preserves magnitude, keeps harmonics)
            double colored = saturated - preGain * (red * hpRed + blue * lpBlue);

            // BLEND clean and colored (THE MAGIC!)
            double y = clean * (1.0 - colorAmt) + colored * colorAmt;

            upsampleBuffer_[i] = y;
        }

        oversampler_.processDown(upsampleBuffer_.data(), M, out);
    }

    double processSample(double xin) {
        double out = 0.0;
        processBlock(&xin, &out, 1);
        return out;
    }

private:
    double processSampleInternal(double s) {
        s *= drive_;
        s -= dcState_ * 1e-4;
        dcState_ = 0.9999 * dcState_ + 1e-4 * s;

        // PARALLEL BLEND (like DM2+)
        double colorAmt = std::abs(character_) * 0.85;
        double clean = s;

        // Colored path
        lpRedState_  += redAlpha_  * (s - lpRedState_);
        double hpRed  = s - lpRedState_;
        lpBlueState_ += blueAlpha_ * (s - lpBlueState_);
        double lpBlue = lpBlueState_;

        double red  = std::max(0.0,  character_);
        double blue = std::max(0.0, -character_);

        double preGain = 0.5;
        double pre = s + preGain * (red * hpRed + blue * lpBlue);
        double saturated = core_.process(pre);
        double colored = saturated - preGain * (red * hpRed + blue * lpBlue);

        // BLEND
        return clean * (1.0 - colorAmt) + colored * colorAmt;
    }

    void updateAlphas() {
        auto a_from_hz = [this](double hz){
            double a = 1.0 - std::exp(-2.0*M_PI*hz / fs_);
            return clampd(a, 1e-6, 1.0);
        };
        redAlpha_  = a_from_hz(redHz_);
        blueAlpha_ = a_from_hz(blueHz_);
    }

    void updateCoreFromCharacter() {
        double c = character_;
        double red  = std::max(0.0,  c);
        double blue = std::max(0.0, -c);

        // Flux bias gain: stronger for Blue (even-order), milder for Red
        double biasGain = 0.05 * blue + 0.02 * red;
        core_.setFluxBiasGain(clampd(biasGain, 0.01, 0.10));

        // Zener drive: only for Red (odd-order asymmetry)
        double zener = 0.6 * red;
        core_.setZenerDrive(clampd(zener, 0.0, 0.8));
    }

    double fs_ = 44100.0;
    BufferedPolyphaseSIMD oversampler_;
    int oversampleFactor_ = 2;  // Default 2× (was 8×) - better CPU/quality balance

    // Pre-allocated buffer for oversampling (eliminates per-sample allocation)
    std::vector<double> upsampleBuffer_;

    // Parameters
    double drive_ = 1.0;
    double character_ = 0.0;  // -1.0 (Blue) to +1.0 (Red)

    // Silk filters
    double redHz_ = 3500.0;
    double blueHz_ = 140.0;
    double redAlpha_ = 0.0;
    double blueAlpha_ = 0.0;
    double lpRedState_ = 0.0;
    double lpBlueState_ = 0.0;

    // DC blocker
    double dcState_ = 0.0;

    // Core v3
    NeveTransformerCore_v3 core_;
};

// VCV Rack Integration Wrapper
struct NeveEngine {
    // Operating mode (0 = Master Output, 1 = Channel Output)
    int outputMode = 0;

    // Sample rate
    float sampleRate = 44100.0f;

    // Oversampling factor
    int oversampleFactor = 8;

    // Stereo engines
    NEVE8816_Engine_Pro_SIMD_v3 engineL;
    NEVE8816_Engine_Pro_SIMD_v3 engineR;

    NeveEngine() : engineL(44100.0, 2), engineR(44100.0, 2) {
        reset();
    }

    void reset() {
        engineL.reset();
        engineR.reset();
    }

    void setOutputMode(int mode) {
        outputMode = mode;
    }

    void setSampleRate(float sr) {
        sampleRate = sr;
        engineL.setSampleRate(double(sr));
        engineR.setSampleRate(double(sr));
    }

    void setOversampleFactor(int f) {
        oversampleFactor = f;
        engineL.setOversampleFactor(f);
        engineR.setOversampleFactor(f);
    }

    // Process stereo audio
    void process(float& left, float& right, float drive, float character) {
        // Mode-dependent drive scaling
        double driveDb = 0.0;
        if (outputMode == 0) {
            // Master mode: conservative (0dB to 12dB)
            driveDb = double(drive) * 12.0;
        } else {
            // Channel mode: aggressive (0dB to 24dB)
            driveDb = double(drive) * 24.0;
        }

        // Character mapping: 0.0→1.0 maps to -1.0→+1.0 (Blue→Red)
        double charNeve = double(character) * 2.0 - 1.0;

        // Apply settings
        engineL.setDriveDb(driveDb);
        engineR.setDriveDb(driveDb);
        engineL.setCharacter(charNeve);
        engineR.setCharacter(charNeve);

        // Process
        double inL = double(left);
        double inR = double(right);
        double outL = 0.0;
        double outR = 0.0;

        engineL.processBlock(&inL, &outL, 1);
        engineR.processBlock(&inR, &outR, 1);

        // Convert back to float and apply VCV Rack voltage compliance
        left = clamp(float(outL), -10.0f, 10.0f);
        right = clamp(float(outR), -10.0f, 10.0f);
    }
};

} // namespace ChanOutNeve
