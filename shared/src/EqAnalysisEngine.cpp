#include "EqAnalysisEngine.hpp"

EqAnalysisEngine::EqAnalysisEngine() : fft(BUFFER_SIZE) {
    startWorkerThread();
}

EqAnalysisEngine::~EqAnalysisEngine() {
    stopWorkerThread();
}

void EqAnalysisEngine::startWorkerThread() {
    if (!workerRunning) {
        workerStop = false;
        workerThread = std::thread(&EqAnalysisEngine::workerThreadFunc, this);
        workerRunning = true;
    }
}

void EqAnalysisEngine::stopWorkerThread() {
    if (workerRunning) {
        workerStop = true;
        workerCV.notify_one();
        if (workerThread.joinable()) {
            workerThread.join();
        }
        workerRunning = false;
    }
}

void EqAnalysisEngine::addSample(float left, float right) {
    leftBuffer[bufferIndex] = left;
    rightBuffer[bufferIndex] = right;
    bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
    frameIndex++;
    if (frameIndex >= frameCount) {
        frameIndex = 0;
        sendToWorker();
    }
}

void EqAnalysisEngine::sendToWorker() {
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        for (int i = 0; i < BUFFER_SIZE; i++) {
            leftWorkerBuffer[i] = leftBuffer[i];
            rightWorkerBuffer[i] = rightBuffer[i];
        }
        newDataReady = true;
    }
    workerCV.notify_one();
}

void EqAnalysisEngine::workerThreadFunc() {
    while (!workerStop) {
        std::unique_lock<std::mutex> lock(bufferMutex);
        workerCV.wait(lock, [this] { return newDataReady.load() || workerStop.load(); });
        if (workerStop) break;
        if (newDataReady) {
            newDataReady = false;
            float leftLocalBuffer[BUFFER_SIZE];
            float rightLocalBuffer[BUFFER_SIZE];
            for (int i = 0; i < BUFFER_SIZE; i++) {
                leftLocalBuffer[i] = leftWorkerBuffer[i];
                rightLocalBuffer[i] = rightWorkerBuffer[i];
            }
            lock.unlock();
            processFFTWorker(leftLocalBuffer, rightLocalBuffer);
        }
    }
}

void EqAnalysisEngine::processFFTWorker(float* leftInputBuffer, float* rightInputBuffer) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
        fftInput[i] = leftInputBuffer[i];
    }
    fft.rfft(fftInput, fftOutput);
    for (int i = 0; i < BUFFER_SIZE / 2; i++) {
        float real = fftOutput[2 * i];
        float imag = fftOutput[2 * i + 1];
        spectrum[i] = sqrtf(real * real + imag * imag) / BUFFER_SIZE;
    }
    mapToLogScale(true);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        fftInput[i] = rightInputBuffer[i];
    }
    fft.rfft(fftInput, fftOutput);
    for (int i = 0; i < BUFFER_SIZE / 2; i++) {
        float real = fftOutput[2 * i];
        float imag = fftOutput[2 * i + 1];
        spectrum[i] = sqrtf(real * real + imag * imag) / BUFFER_SIZE;
    }
    mapToLogScale(false);
}

void EqAnalysisEngine::mapToLogScale(bool isLeftChannel) {
    std::lock_guard<std::mutex> lock(spectrumMutex);
    float* targetSpectrum = isLeftChannel ? leftLogSpectrum : rightLogSpectrum;
    for (int i = 0; i < DISPLAY_BANDS; i++) {
        targetSpectrum[i] = 0.0f;
    }
    for (int bin = 1; bin < BUFFER_SIZE / 2; bin++) {
        float frequency = (float)bin * sampleRate / (float)BUFFER_SIZE;
        if (frequency < MIN_FREQ || frequency > MAX_FREQ) continue;
        float logFreq = log10f(frequency);
        float bandPos = (logFreq - MIN_LOG_FREQ) / (MAX_LOG_FREQ - MIN_LOG_FREQ) * (float)(DISPLAY_BANDS - 1);
        int band = (int)roundf(bandPos);
        band = clamp(band, 0, DISPLAY_BANDS - 1);
        targetSpectrum[band] = std::max(targetSpectrum[band], spectrum[bin]);
    }
    float* peakHold = isLeftChannel ? leftPeakHold : rightPeakHold;
    float* peakTimer = isLeftChannel ? leftPeakTimer : rightPeakTimer;
    for (int i = 0; i < DISPLAY_BANDS; i++) {
        targetSpectrum[i] *= 0.9999f;
        if (targetSpectrum[i] > peakHold[i]) {
            peakHold[i] = targetSpectrum[i];
            peakTimer[i] = 0.5f;
        } else {
            peakTimer[i] -= 1.0f / 60.0f;
            if (peakTimer[i] <= 0.0f) {
                peakHold[i] = std::max(targetSpectrum[i], peakHold[i] * 0.98f);
            }
        }
    }
}

void EqAnalysisEngine::getSpectrumData(float* leftSpectrum, float* rightSpectrum,
                                     float* leftPeaks, float* rightPeaks) {
    std::lock_guard<std::mutex> lock(spectrumMutex);
    if (leftSpectrum) {
        for (int i = 0; i < DISPLAY_BANDS; i++) {
            leftSpectrum[i] = leftLogSpectrum[i];
        }
    }
    if (rightSpectrum) {
        for (int i = 0; i < DISPLAY_BANDS; i++) {
            rightSpectrum[i] = rightLogSpectrum[i];
        }
    }
    if (leftPeaks) {
        for (int i = 0; i < DISPLAY_BANDS; i++) {
            leftPeaks[i] = leftPeakHold[i];
        }
    }
    if (rightPeaks) {
        for (int i = 0; i < DISPLAY_BANDS; i++) {
            rightPeaks[i] = rightPeakHold[i];
        }
    }
}