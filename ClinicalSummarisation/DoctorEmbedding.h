#pragma once
#include <vector>
#include <string>
#include <winrt/Windows.Foundation.h> 
#include "miniaudio.h" // Include the header, but DO NOT define IMPLEMENTATION here

// Forward declaration
class SpeakerEncoder;

class DoctorEmbedding {
public:
    winrt::fire_and_forget EnrollNewSpeakerAsync(SpeakerEncoder* encoder);
    void FinishEnrollmentEarly();
    void CancelEnrollment();

    std::vector<float> getSpeachEmbedding();

private:
    std::vector<float> m_speachEmbedding;
    std::atomic<bool> m_finishEarly{ false };
    std::atomic<bool> m_cancel{ false };

    // Helpers
    void SaveToDisk(const std::vector<float>& embedding);
    std::vector<float> LoadFromDisk();
    std::string getFilePath();

    // Miniaudio Callback Context
    struct EnrollmentContext {
        std::vector<float> audioBuffer;
        bool isRecording = false;
        size_t maxSamples = 16000 * 30; // 30 Seconds at 16kHz
    };

    // Static callback for Miniaudio
    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};