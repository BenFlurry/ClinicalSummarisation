#pragma once
#include "AudioTranscriptionBridge.h"
#include <thread>
#include <atomic>
#include <sstream>

#include "openvino/genai/whisper_pipeline.hpp"

class TranscriptionEngine {
private:
	AudioTranscriptionBridge* m_bridge;
	std::stringstream m_fullTranscript;
	std::atomic<bool> m_isRunning;

	ov::genai::WhisperPipeline* m_pipeline = nullptr;

public:
	TranscriptionEngine(AudioTranscriptionBridge* bridgePtr);
	~TranscriptionEngine();

	void InitialiseModel();
	std::string ProcessLoop();
};

