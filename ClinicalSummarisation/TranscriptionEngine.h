#pragma once
#include "AudioTranscriptionBridge.h"
#include <thread>
#include <atomic>
#include <sstream>

#include "openvino/genai/whisper_pipeline.hpp"
#include "SpeakerEncoder.h"

class TranscriptionEngine {
public:
	TranscriptionEngine(AudioTranscriptionBridge* bridgePtr);
	~TranscriptionEngine();

	void InitialiseModel();
	SpeakerEncoder* GetEncoder() { return m_speakerEncoder; }
	std::string ProcessLoop();
	void SetDoctorProfile(const std::vector<float>& profile) { m_doctorProfile = profile;  }

private:
	AudioTranscriptionBridge* m_bridge;
	SpeakerEncoder* m_speakerEncoder;
	std::stringstream m_fullTranscript;
	std::atomic<bool> m_isRunning;

	ov::genai::WhisperPipeline* m_pipeline = nullptr;
	std::vector<float> m_doctorProfile;

};

