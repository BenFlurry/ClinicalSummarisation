#pragma once
#include "AudioTranscriptionBridge.h"
#include <vector>
#include <mutex> 

#include "miniaudio.h" 

class AudioRecorder {
private:
    AudioTranscriptionBridge* m_bridge;
    ma_device m_device;
    ma_device_config m_config;
    std::vector<float> m_currentBuffer;
    bool m_isRecording = false;

    // The Lock
    std::mutex m_bufferMutex;
    std::string m_microphoneName;

public:
    AudioRecorder(AudioTranscriptionBridge* bridgePtr);
    ~AudioRecorder();
    void Start();
    void Stop();
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void SetMicrophoneName(std::string microphoneName);

private:
    void Flush(bool isLast);
    void SaveToWav(const std::vector<float>& audioData);
};