
#include "pch.h"
#include "AudioRecordingThread.h"

//#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <winrt/Windows.Storage.h>

// WAV Header Helper (Standard 44-byte header)
struct WavHeader {
    char riff[4] = { 'R', 'I', 'F', 'F' };
    uint32_t overallSize;
    char wave[4] = { 'W', 'A', 'V', 'E' };
    char fmt[4] = { 'f', 'm', 't', ' ' };
    uint32_t fmtChunkSize = 16;
    uint16_t formatType = 3; // 3 = IEEE Float
    uint16_t channels = 1;
    uint32_t sampleRate = 16000;
    uint32_t byteRate = 16000 * 4 * 1; // SampleRate * BytesPerSample * Channels
    uint16_t blockAlign = 4;
    uint16_t bitsPerSample = 32;
    char data[4] = { 'd', 'a', 't', 'a' };
    uint32_t dataSize;
};

AudioRecordingThread::AudioRecordingThread() {
    // 1. Configure for Whisper (1 Channel, 16000Hz)
    config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32; // Floating point is standard for AI
    config.capture.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = DataCallback;
    config.pUserData = this; // Pass class instance to callback
}

AudioRecordingThread::~AudioRecordingThread() {
    Stop();
}

bool AudioRecordingThread::Start(int deviceIndex) {
    if (isRecording) return false;

    // Initialize device
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return false;
    }

    chunkCounter = 0;
    currentChunk.clear();
    // Pre-reserve 30s of memory to prevent reallocation lag
    currentChunk.reserve(SAMPLE_RATE * CHUNK_DURATION_SEC);

    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        return false;
    }

    isRecording = true;
    return true;
}

void AudioRecordingThread::Stop() {
    if (!isRecording) return;

    ma_device_uninit(&device); // This stops and unloads the device

    // Save whatever is left in the buffer (e.g., the last 15 seconds)
    if (!currentChunk.empty()) {
        WriteWavFile(currentChunk, chunkCounter++);
    }

    isRecording = false;
}

// This runs on a high-priority background thread managed by Miniaudio
void AudioRecordingThread::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioRecordingThread* self = (AudioRecordingThread*)pDevice->pUserData;
    if (!self->isRecording) return;

    const float* samples = (const float*)pInput;

    std::lock_guard<std::mutex> lock(self->bufferMutex);

    // Append new samples to our buffer
    self->currentChunk.insert(self->currentChunk.end(), samples, samples + frameCount);

    // Check if we hit 30 seconds
    // 16000 samples/sec * 30 sec = 480,000 samples
    size_t samplesNeeded = self->SAMPLE_RATE * self->CHUNK_DURATION_SEC;

    if (self->currentChunk.size() >= samplesNeeded) {
        // Copy exactly 30s worth
        std::vector<float> fileData(self->currentChunk.begin(), self->currentChunk.begin() + samplesNeeded);

        // Remove those 30s from the buffer (keep the overflow)
        self->currentChunk.erase(self->currentChunk.begin(), self->currentChunk.begin() + samplesNeeded);

        // Trigger file save (Should ideally be a separate thread, but fast enough for NVMe/SSD)
        self->WriteWavFile(fileData, self->chunkCounter++);
    }
}

void AudioRecordingThread::WriteWavFile(const std::vector<float>& buffer, int chunkIndex) {
    winrt::hstring localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
    std::wstring localPath(localFolder.begin(), localFolder.end());

    // 3. Construct the full path
    std::filesystem::path fullPath = std::filesystem::path(localPath) / ("recording_" + std::to_string(chunkIndex) + ".wav");

    std::string filename = fullPath.string(); // Convert to string for ofstream
    WavHeader header;
    header.dataSize = buffer.size() * sizeof(float);
    header.overallSize = header.dataSize + 36;

    std::ofstream file(filename, std::ios::binary);
    file.write((char*)&header, sizeof(WavHeader));
    file.write((char*)buffer.data(), buffer.size() * sizeof(float));
    file.close();

    // Notify your main thread here via Dispatcher that a file is ready!
}
