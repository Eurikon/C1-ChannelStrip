#pragma once
#include "rack.hpp"
#include <dsp/fft.hpp>
#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace rack;

class EqAnalysisEngine {
public:
    static const int BUFFER_SIZE = 2048;
    static const int DISPLAY_BANDS = 128;
    static constexpr float MIN_FREQ = 20.0f;
    static constexpr float MAX_FREQ = 22000.0f;
    static constexpr float MIN_LOG_FREQ = 1.30103f;
    static constexpr float MAX_LOG_FREQ = 4.342423f;

    EqAnalysisEngine();
    ~EqAnalysisEngine();

    void addSample(float left, float right);
    void setSampleRate(float sampleRate) { this->sampleRate = sampleRate; }

    // Get spectrum data for rendering (thread-safe)
    const float* getLeftSpectrum() const { return leftLogSpectrum; }
    const float* getRightSpectrum() const { return rightLogSpectrum; }
    const float* getLeftPeakHold() const { return leftPeakHold; }
    const float* getRightPeakHold() const { return rightPeakHold; }

    // Copy spectrum data (thread-safe)
    void getSpectrumData(float* leftSpectrum, float* rightSpectrum,
                        float* leftPeaks = nullptr, float* rightPeaks = nullptr);

    // Thread control (public for external start/stop)
    void startWorkerThread();
    void stopWorkerThread();

private:
    float sampleRate = 44100.0f;
    float leftBuffer[BUFFER_SIZE] = {};
    float rightBuffer[BUFFER_SIZE] = {};
    int bufferIndex = 0;
    int frameIndex = 0;
    int frameCount = 1024;
    dsp::RealFFT fft;
    float fftInput[BUFFER_SIZE] = {};
    float fftOutput[BUFFER_SIZE * 2] = {};
    float spectrum[BUFFER_SIZE / 2] = {};
    float leftLogSpectrum[DISPLAY_BANDS] = {};
    float rightLogSpectrum[DISPLAY_BANDS] = {};
    float leftPeakHold[DISPLAY_BANDS] = {};
    float rightPeakHold[DISPLAY_BANDS] = {};
    float leftPeakTimer[DISPLAY_BANDS] = {};
    float rightPeakTimer[DISPLAY_BANDS] = {};
    std::thread workerThread;
    std::mutex bufferMutex;
    std::mutex spectrumMutex;
    std::condition_variable workerCV;
    std::atomic<bool> workerStop{false};
    std::atomic<bool> newDataReady{false};
    float leftWorkerBuffer[BUFFER_SIZE] = {};
    float rightWorkerBuffer[BUFFER_SIZE] = {};
    bool workerRunning = false;

    void sendToWorker();
    void workerThreadFunc();
    void processFFTWorker(float* leftInputBuffer, float* rightInputBuffer);
    void mapToLogScale(bool isLeftChannel);
};