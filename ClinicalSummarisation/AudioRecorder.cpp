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

void AudioRecorder::SetMicrophoneName(std::string microphoneName) {
    m_microphoneName = microphoneName;
}

// 2. Start Recording
void AudioRecorder::Start() {
    if (m_isRecording) return;

    ma_context context;

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        return; // Failed to init context
    }

    // find microphone ID
    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;

    // Get list of all microphones
    ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount);

    // 2. FIND THE MATCHING DEVICE ID
    ma_device_id* pSelectedDeviceID = NULL;

    // Only look if we actually have a target name set
    if (!m_microphoneName.empty()) {
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            std::string currentName = pCaptureInfos[i].name;

            // Check if names match (WinRT name == Miniaudio name)
            if (currentName == m_microphoneName) {
                pSelectedDeviceID = &pCaptureInfos[i].id;
                break; // Found it!
            }
        }
    }

    // 3. CONFIGURE WITH SPECIFIC ID
    // If pSelectedDeviceID is NULL, it defaults to the system default mic
    m_config = ma_device_config_init(ma_device_type_capture);
    m_config.capture.pDeviceID = pSelectedDeviceID; // <--- The Magic Link
    m_config.capture.format = ma_format_f32;
    m_config.capture.channels = 1;
    m_config.sampleRate = 16000;
    m_config.pUserData = this;
    m_config.dataCallback = DataCallback;

    // 4. INIT DEVICE
    // Note: We use NULL for context here to let miniaudio manage its own internal context 
    // for the device, passing the ID we found is safe on Windows/WASAPI.
    if (ma_device_init(NULL, &m_config, &m_device) != MA_SUCCESS) {
        ma_context_uninit(&context); // Cleanup temp context
        return;
    }

    // Cleanup the lookup context (we don't need it anymore)
    ma_context_uninit(&context);

    // 5. RUN
    m_currentBuffer.clear();
    m_currentBuffer.reserve(16000 * 30);

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
        AudioRecorder::SaveToWav(m_currentBuffer);

        // 3. NOW flush (which sends it to the AI and clears the buffer)
        Flush(true);
    }
}
void AudioRecorder::SaveToWav(const std::vector<float>& buffer) {
    // 1. Check for empty data (common reason for "missing" files)
    if (buffer.empty()) {
        OutputDebugStringA("!!! ERROR: SaveToWav called with EMPTY buffer. No recording data found. !!!\n");
        return;
    }

    try {
        // 2. Get the LocalFolder path safely
        auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();

        // 3. Construct the path properly
        // NOTE: We do NOT convert this to std::string yet to preserve special characters
        std::filesystem::path fullPath = std::filesystem::path(localFolder.c_str()) / "debug_audio.wav";

        // 4. Setup Header
        WavHeader header;
        header.dataSize = (uint32_t)(buffer.size() * sizeof(float));
        header.overallSize = header.dataSize + 36;

        // 5. Open File using the PATH object (Fixes the encoding bug)
        // MSVC supports passing the wide-char path directly here.
        std::ofstream file(fullPath, std::ios::binary);

        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
            file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(float));
            file.close();

            // 6. Log the EXACT path to confirm success
            std::wstring widePath = fullPath.wstring(); // Convert to wstring for safe printing
            OutputDebugStringW(L"\n=========================================\n");
            OutputDebugStringW((L"SUCCESS: Saved WAV to:\n" + widePath).c_str());
            OutputDebugStringW(L"\n=========================================\n");
        }
        else {
            // If we get here, the path is likely locked or invalid
            OutputDebugStringA("!!! ERROR: Could not open file for writing. Check permissions or if file is open in another app. !!!\n");
        }
    }
    catch (const std::exception& e) {
        std::string err = "!!! EXCEPTION: " + std::string(e.what()) + " !!!\n";
        OutputDebugStringA(err.c_str());
    }
    catch (...) {
        OutputDebugStringA("!!! UNKNOWN EXCEPTION while saving WAV file !!!\n");
    }
}

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
