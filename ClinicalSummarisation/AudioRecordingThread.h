// NO LONGER IN USE
#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include "miniaudio.h"

class AudioRecordingThread {
public:
    AudioRecordingThread();
    ~AudioRecordingThread();

    // Starts the recording thread
    bool Start(int deviceIndex = -1); // -1 = Default Device
    void Stop();

private:
    // Miniaudio callbacks need to be static, so we pass 'this' as user data
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    void WriteWavFile(const std::vector<float>& buffer, int chunkIndex);

    ma_device device;
    ma_device_config config;

    std::vector<float> currentChunk;
    std::atomic<bool> isRecording{ false };
    std::mutex bufferMutex;

    int chunkCounter = 0;
    const int SAMPLE_RATE = 16000; // Whisper loves 16kHz
    const int CHUNK_DURATION_SEC = 30;
};

