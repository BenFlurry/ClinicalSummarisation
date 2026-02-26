#pragma once
#include <vector>
#include <string>
#include <winrt/Windows.Foundation.h> 
#include "miniaudio.h" 

// declare so we can use
class SpeakerEncoder;

class DoctorEmbedding {
public:
    winrt::fire_and_forget EnrollNewSpeakerAsync(SpeakerEncoder* encoder);
    void FinishEnrollmentEarly();
    void CancelEnrollment();

    std::vector<float> getSpeachEmbedding();
    bool IsProfileEnrolled();

private:
    std::vector<float> m_speachEmbedding;
    std::atomic<bool> m_finishEarly{ false };
    std::atomic<bool> m_cancel{ false };

    // helpers
    void SaveToDisk(const std::vector<float>& embedding);
    std::vector<float> LoadFromDisk();
    std::string getFilePath();

    // mini audio callback context
    struct EnrollmentContext {
        std::vector<float> audioBuffer;
        bool isRecording = false;
        // 30s at 16kHz
        size_t maxSamples = 16000 * 30; 
    };

    // static callback for Miniaudio
    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};