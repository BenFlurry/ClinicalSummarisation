#include "pch.h"
#include "AudioRecorder.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <iostream>
#include <fstream>
#include <winrt/WIndows.Storage.h>
#include <filesystem>

// standard wav header - not used
struct WavHeader {
    char riff[4] = { 'R', 'I', 'F', 'F' };
    uint32_t overallSize;
    char wave[4] = { 'W', 'A', 'V', 'E' };
    char fmt[4] = { 'f', 'm', 't', ' ' };
    uint32_t fmtChunkSize = 16;
    // IEEE float
    uint16_t formatType = 3; 
    uint16_t channels = 1;
    uint32_t sampleRate = 16000;
    // sample rate x bytes per sample x channels
    uint32_t byteRate = 16000 * 4 * 1; 
    uint16_t blockAlign = 4;
    uint16_t bitsPerSample = 32;
    char data[4] = { 'd', 'a', 't', 'a' };
    uint32_t dataSize;
};

AudioRecorder::AudioRecorder(AudioTranscriptionBridge* bridgePtr) {
    // bridge between auto recording and transcription
    m_bridge = bridgePtr;

    m_isRecording = false;
    m_device = {};

    // mini audio config
    m_config = ma_device_config_init(ma_device_type_capture);
    m_config.capture.format = ma_format_f32; 
    m_config.capture.channels = 1;          
    m_config.sampleRate = 16000;           

    m_config.pUserData = this;
    m_config.dataCallback = DataCallback;
}

AudioRecorder::~AudioRecorder() {
    Stop();
}

void AudioRecorder::SetMicrophoneName(std::string microphoneName) { m_microphoneName = microphoneName; }


void AudioRecorder::Start() {
    if (m_isRecording) return;

    ma_context context;

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        // failed to initialise audio capture, return
        return; 
    }

    // find microphone ID
    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;

    // get list of all microphones
    ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount);

    // find matching device id
    ma_device_id* pSelectedDeviceID = NULL;

    if (!m_microphoneName.empty()) {
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            std::string currentName = pCaptureInfos[i].name;

            // check if name matches
            if (currentName == m_microphoneName) {
                pSelectedDeviceID = &pCaptureInfos[i].id;
                break; 
            }
        }
    }

    // if we didnt find our mic id or if one not given, it defaults to default audio device on windows
    m_config = ma_device_config_init(ma_device_type_capture);
    // apply found device
    m_config.capture.pDeviceID = pSelectedDeviceID;
    m_config.capture.format = ma_format_f32;
    m_config.capture.channels = 1;
    m_config.sampleRate = 16000;
    m_config.pUserData = this;
    m_config.dataCallback = DataCallback;

    // initialise device
    if (ma_device_init(NULL, &m_config, &m_device) != MA_SUCCESS) {
        // clean up temp context
        ma_context_uninit(&context); 
        return;
    }

    // clean up lookup context
    ma_context_uninit(&context);

    // run
    m_currentBuffer.clear();
    m_currentBuffer.reserve(16000 * 30);

    if (ma_device_start(&m_device) != MA_SUCCESS) {
        ma_device_uninit(&m_device);
        return;
    }

    m_isRecording = true;
}


// used by mini audio to handle receiving audio date
void AudioRecorder::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioRecorder* self = (AudioRecorder*)pDevice->pUserData;
    if (!self->m_isRecording) return;

    const float* samples = (const float*)pInput;

    std::lock_guard<std::mutex> lock(self->m_bufferMutex);

    self->m_currentBuffer.insert(self->m_currentBuffer.end(), samples, samples + frameCount);

    if (self->m_currentBuffer.size() >= 16000 * 30) {
        self->Flush(false);
    }
}

void AudioRecorder::Stop() {
    if (!m_isRecording) return;

    // stop mini audio recording
    ma_device_uninit(&m_device);
    m_isRecording = false;

	std::lock_guard<std::mutex> lock(m_bufferMutex);

    // save to wav for debugging purposes
	AudioRecorder::SaveToWav(m_currentBuffer);

    // flush to send to transcription
	Flush(true);
}

// temp helper method to save audio to a WAV file for debugging
void AudioRecorder::SaveToWav(const std::vector<float>& buffer) {
    if (buffer.empty()) { return; }

	auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
	std::filesystem::path fullPath = std::filesystem::path(localFolder.c_str()) / "debug_audio.wav";

	WavHeader header;
	header.dataSize = (uint32_t)(buffer.size() * sizeof(float));
	header.overallSize = header.dataSize + 36;

	std::ofstream file(fullPath, std::ios::binary);

	file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
	file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(float));
	file.close();
}

void AudioRecorder::Flush(bool isLast) {
    AudioChunk chunk;

    size_t samplesNeeded = 16000 * 30;

    if (m_currentBuffer.size() >= samplesNeeded) {
        // copy 30 seconds from buffer
        chunk.audioData.assign(m_currentBuffer.begin(), m_currentBuffer.begin() + samplesNeeded);

        // remove 30s from buffer
        m_currentBuffer.erase(m_currentBuffer.begin(), m_currentBuffer.begin() + samplesNeeded);
    }
    else {
        // end of stream so flush buffer
        chunk.audioData = m_currentBuffer;
        m_currentBuffer.clear();
    }

    chunk.isLastChunk = isLast;

    // push to bridge for transcription
	m_bridge->Push(chunk);
}
