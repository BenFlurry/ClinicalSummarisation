#include "pch.h"
#include "TranscriptionEngine.h"
#include "Helpers.h"
#include <iostream>

TranscriptionEngine::TranscriptionEngine(AudioTranscriptionBridge* bridgePtr) {
	m_bridge = bridgePtr;
	m_isRunning = false;
}

TranscriptionEngine::~TranscriptionEngine() {
	if (m_pipeline) {
		delete m_pipeline;
	}
}

void TranscriptionEngine::InitialiseModel() {
	std::filesystem::path modelPath = Helpers::GetModelPath("whisper_stateful");
	m_pipeline = new ov::genai::WhisperPipeline(modelPath, "CPU");
}

std::string TranscriptionEngine::ProcessLoop() {
    m_fullTranscript.str("");
    m_isRunning = true;

    OutputDebugStringA("--- THREAD STARTED ---\n");

    while (m_isRunning) {
        // checkpoint 1
        AudioChunk chunk = m_bridge->Pop();

        if (m_pipeline && !chunk.audioData.empty()) {
            OutputDebugStringA("--- PROCESSING CHUNK ---\n"); // Checkpoint 2
            float max_amplitude = 0.0f;
            for (float sample : chunk.audioData) {
                if (std::abs(sample) > max_amplitude) max_amplitude = std::abs(sample);
            }

            m_fullTranscript << "Max amp" << " ";
            m_fullTranscript << std::to_string(max_amplitude) << " ";

            try {
                ov::genai::WhisperGenerationConfig config;
                config.max_new_tokens = 100; // Limit tokens to prevent infinite loops
                config.task = "transcribe";
                //config.language = "<|en|>";

                // *** DANGER ZONE: This is where NPU usually crashes ***
                OutputDebugStringA("--- RUNNING GENERATE (This may take time) ---\n");

                auto result = m_pipeline->generate(chunk.audioData, config);

                OutputDebugStringA("--- GENERATE SUCCESS ---\n"); // Checkpoint 3

                if (!result.texts.empty()) {
                    std::string text = result.texts[0];
                    if (!text.empty()) {
                        m_fullTranscript << text << " ";
                        OutputDebugStringA(("Transcribed: " + text + "\n").c_str());
                    }
                    else {
                        m_fullTranscript << "empty text" << " ";
                    }
                }
                else {
                    m_fullTranscript << "Nothing heard" << " ";
                }
            }
            catch (const std::exception& e) {
                // Catch standard C++ errors (OpenVINO throws these)
                OutputDebugStringA("!!! EXCEPTION CAUGHT IN THREAD !!!\n");
                OutputDebugStringA(e.what());
                OutputDebugStringA("\n");
				m_fullTranscript << "thread exception" << " ";
				m_fullTranscript << e.what() << " ";
            }
            catch (...) {
                // Catch hard crashes (SEH / Memory violations)
                OutputDebugStringA("!!! UNKNOWN FATAL ERROR IN THREAD !!!\n");
				m_fullTranscript << "unkown fatal" << " ";
            }
        }

        if (chunk.isLastChunk) {
            OutputDebugStringA("--- LAST CHUNK SIGNAL RECEIVED ---\n");
            m_isRunning = false;
			m_fullTranscript << "last chunk" << " ";
            if (chunk.audioData.empty()) {
				m_fullTranscript << "empty" << " ";
            }
            if (!m_pipeline) {
				m_fullTranscript << "no pipeline" << " ";
            }
        }
    }

    OutputDebugStringA("--- THREAD EXITING ---\n");
    return m_fullTranscript.str();
}