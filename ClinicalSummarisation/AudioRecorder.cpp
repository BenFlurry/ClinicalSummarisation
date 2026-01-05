#include "pch.h"
#include "AudioRecorder.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <iostream>
#include <fstream>
#include <winrt/WIndows.Storage.h>
#include <filesystem>

struct WavHeader {
    char riff[4] = { 'R', 'I', 'F', 'F' };
    uint32_t overallSize;
    char wave[4] = { 'W', 'A', 'V', 'E' };
    char fmt[4] = { 'f', 'm', 't', ' ' };
    uint32_t fmtChunkSize = 16;
    uint16_t formatType = 3; // 3 = IEEE Float (Matches your miniaudio format)
    uint16_t channels = 1;
    uint32_t sampleRate = 16000;
    uint32_t byteRate = 16000 * 4 * 1; // SampleRate * BytesPerSample * Channels
    uint16_t blockAlign = 4;
    uint16_t bitsPerSample = 32;
    char data[4] = { 'd', 'a', 't', 'a' };
    uint32_t dataSize;
};

AudioRecorder::AudioRecorder(AudioTranscriptionBridge* bridgePtr) {
    m_bridge = bridgePtr;

    m_isRecording = false;
    m_device = {};

    // Initialize Miniaudio Configuration
    m_config = ma_device_config_init(ma_device_type_capture);
    m_config.capture.format = ma_format_f32; // IEEE Float (Standard for AI models like Whisper)
    m_config.capture.channels = 1;           // Mono
    m_config.sampleRate = 16000;             // Whisper native rate

    // We pass 'this' so the static callback can access our member variables
    m_config.pUserData = this;
    m_config.dataCallback = DataCallback;
}

// Destructor: Ensures we stop recording if the object is deleted
AudioRecorder::~AudioRecorder() {
    Stop();
}

// 2. Start Recording
void AudioRecorder::Start() {
    if (m_isRecording) return;

    // Initialize the device
    if (ma_device_init(NULL, &m_config, &m_device) != MA_SUCCESS) {
        // Handle error (throw exception or log to output)
        return;
    }

    // Clear buffer and reserve 30s of memory to avoid re-allocations during recording
    m_currentBuffer.clear();
    m_currentBuffer.reserve(16000 * 30);

    // Start the hardware
    if (ma_device_start(&m_device) != MA_SUCCESS) {
        ma_device_uninit(&m_device);
        return;
    }

    m_isRecording = true;
}

void AudioRecorder::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioRecorder* self = (AudioRecorder*)pDevice->pUserData;
    if (!self->m_isRecording) return;

    const float* samples = (const float*)pInput;

    // LOCK: Protect the buffer while writing
    std::lock_guard<std::mutex> lock(self->m_bufferMutex);

    self->m_currentBuffer.insert(self->m_currentBuffer.end(), samples, samples + frameCount);

    if (self->m_currentBuffer.size() >= 16000 * 30) {
        self->Flush(false);
    }
}

void AudioRecorder::Stop() {
    if (!m_isRecording) return;

    // 1. Stop the hardware first 
    // (This ensures no new data is being written to m_currentBuffer)
    ma_device_uninit(&m_device);
    m_isRecording = false;

    // 2. Lock the mutex to safely access the buffer
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);

        // --- CALL IT HERE (Safe & Secure) ---
        // We save the data while it is frozen and complete
        SaveToWav(m_currentBuffer);

        // 3. NOW flush (which sends it to the AI and clears the buffer)
        Flush(true);
    }
}
void AudioRecorder::SaveToWav(const std::vector<float>& buffer) {
    if (buffer.empty()) return;

    // 1. Get the Music Library Path (Easier to find than LocalFolder)
    auto musicFolder = winrt::Windows::Storage::KnownFolders::MusicLibrary().Path();
    std::filesystem::path fullPath = std::filesystem::path(musicFolder.c_str()) / "debug_audio.wav";
    std::string filename = fullPath.string();

    // 2. Prepare the Header using the Struct
    WavHeader header;
    header.dataSize = (uint32_t)(buffer.size() * sizeof(float));
    header.overallSize = header.dataSize + 36; // 36 = size of header minus first 8 bytes

    // 3. Write the file
    std::ofstream file(filename, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(float));
        file.close();

        OutputDebugStringA(("--- SAVED WAV TO: " + filename + " ---\n").c_str());
    }
    else {
        OutputDebugStringA("--- FAILED TO OPEN FILE FOR WRITING ---\n");
    }
}
// 5. Flush (Helper to bundle data and push to bridge)
void AudioRecorder::Flush(bool isLast) {
    AudioChunk chunk;

    size_t samplesNeeded = 16000 * 30;

    if (m_currentBuffer.size() >= samplesNeeded) {
        // Copy first 30s
        chunk.audioData.assign(m_currentBuffer.begin(), m_currentBuffer.begin() + samplesNeeded);

        // Remove first 30s from buffer, keep the rest (overflow) for the next packet
        m_currentBuffer.erase(m_currentBuffer.begin(), m_currentBuffer.begin() + samplesNeeded);
    }
    else {
        // Take everything (End of stream)
        chunk.audioData = m_currentBuffer;
        m_currentBuffer.clear();
    }

    chunk.isLastChunk = isLast;

    // --- PUSH TO BRIDGE ---
	m_bridge->Push(chunk);
}