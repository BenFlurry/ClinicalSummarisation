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
	std::filesystem::path modelPath = Helpers::GetModelPath("whisper-medium-i8");
	m_pipeline = new ov::genai::WhisperPipeline(modelPath, "CPU");
}

//std::string TranscriptionEngine::ProcessLoop() {
//    m_fullTranscript.str("");
//    m_isRunning = true;
//
//    OutputDebugStringA("--- THREAD STARTED ---\n");
//
//    while (m_isRunning) {
//        // checkpoint 1
//        AudioChunk chunk = m_bridge->Pop();
//
//        m_fullTranscript << "audio size:" << " ";
//        m_fullTranscript << std::to_string(chunk.audioData.size()) << " ";
//
//        if (m_pipeline && !chunk.audioData.empty()) {
//            float max_amplitude = 0.0f;
//            for (float sample : chunk.audioData) {
//                if (std::abs(sample) > max_amplitude) max_amplitude = std::abs(sample);
//            }
//
//            m_fullTranscript << "Max amp" << " ";
//            m_fullTranscript << std::to_string(max_amplitude) << " ";
//
//            //if (max_amplitude > 0.01f) {
//            //    float gain = 0.95f / max_amplitude; // Target 95% volume
//            //    for (size_t i = 0; i < chunk.audioData.size(); i++) {
//            //        chunk.audioData[i] *= gain;
//            //    }
//            //}
//
//			size_t targetSampleCount = 16000 * 31;
//
//			size_t originalSize = chunk.audioData.size();
//
//			// Resize the vector (fills new space with 0.0f silence)
//			chunk.audioData.resize(targetSampleCount, 0.0f);
//
//
//            try {
//                ov::genai::WhisperGenerationConfig config;
//                config.max_new_tokens = 100; // Limit tokens to prevent infinite loops
//                config.task = "transcribe";
//                //config.language = "en";
//                config.return_timestamps = true;
//
//
//
//                auto result = m_pipeline->generate(chunk.audioData, config);
//
//                OutputDebugStringA("--- GENERATE SUCCESS ---\n"); // Checkpoint 3
//
//                if (!result.texts.empty()) {
//                    //m_fullTranscript << "size of result:" << " ";
//                    //m_fullTranscript << std::to_string(result.texts.size()) << " ";
//
//                    //m_fullTranscript << "size of chunk" << " ";
//                    
//                    //auto chunk = &result.chunks;
//                    //m_fullTranscript << std::to_string(result.chunks.value().size()) << " ";
//
//                    //for (auto& chunk : *result.chunks) {
//                    //    m_fullTranscript << "timestamps: [" << chunk.start_ts << ", " << chunk.end_ts << "] text: " << chunk.text << "\n";
//                    //}
//
//
//                    std::string text = result.texts[0];
//                    if (!text.empty()) {
//                        m_fullTranscript << text << " ";
//                        OutputDebugStringA(("Transcribed: " + text + "\n").c_str());
//                    }
//                    else {
//                        m_fullTranscript << "empty text" << " ";
//                    }
//                }
//                else {
//                    m_fullTranscript << "Nothing heard" << " ";
//                }
//            }
//            catch (const std::exception& e) {
//                // Catch standard C++ errors (OpenVINO throws these)
//                OutputDebugStringA("!!! EXCEPTION CAUGHT IN THREAD !!!\n");
//                OutputDebugStringA(e.what());
//                OutputDebugStringA("\n");
//				m_fullTranscript << "thread exception" << " ";
//				m_fullTranscript << e.what() << " ";
//            }
//            catch (...) {
//                // Catch hard crashes (SEH / Memory violations)
//                OutputDebugStringA("!!! UNKNOWN FATAL ERROR IN THREAD !!!\n");
//				m_fullTranscript << "unkown fatal" << " ";
//            }
//        }
//
//        if (chunk.isLastChunk) {
//            OutputDebugStringA("--- LAST CHUNK SIGNAL RECEIVED ---\n");
//            m_isRunning = false;
//			m_fullTranscript << "last chunk" << " ";
//            if (chunk.audioData.empty()) {
//				m_fullTranscript << "empty" << " ";
//            }
//            if (!m_pipeline) {
//				m_fullTranscript << "no pipeline" << " ";
//            }
//        }
//    }
//
//    OutputDebugStringA("--- THREAD EXITING ---\n");
//    return m_fullTranscript.str();
//}

std::string TranscriptionEngine::ProcessLoop() {
    // Clear previous text
    m_fullTranscript.str("");
    m_isRunning = true;

    // DEBUG: Log thread start
    m_fullTranscript << "[Start] ";

    while (m_isRunning) {
        AudioChunk chunk = m_bridge->Pop();

        // Only process if valid
        if (m_pipeline && !chunk.audioData.empty()) {

            // 1. DEBUG: Log Input Size
            size_t originalSize = chunk.audioData.size();
            m_fullTranscript << "\n[In: " << std::to_string(originalSize) << "] ";

            // 2. DEBUG: Log Amplitude (Volume)
            float max_amp = 0.0f;
            for (float s : chunk.audioData) max_amp = std::max(max_amp, std::abs(s));
            m_fullTranscript << "[Amp: " << std::to_string(max_amp).substr(0, 5) << "] ";

            // 3. LOGIC: Normalization (Boost volume if too low)
            if (max_amp > 0.001f && max_amp < 0.9f) {
                float gain = 0.9f / max_amp;
                for (float& s : chunk.audioData) s *= gain;
                m_fullTranscript << "[Boosted] ";
            }

            // 4. LOGIC: Padding (Fix for short clips)
            size_t target_samples = 16000 * 30; // 30 seconds
            if (chunk.audioData.size() < target_samples) {
                chunk.audioData.resize(target_samples, 0.0f);
                m_fullTranscript << "[Padded] ";
            }

            m_fullTranscript << "[Type: " << typeid(chunk.audioData[0]).name()
                << " Size: " << sizeof(chunk.audioData[0]) << "b] ";

            try {
                ov::genai::WhisperGenerationConfig config;
                config.max_new_tokens = 100;
                config.task = "transcribe";
                config.return_timestamps = true;
                config.suppress_tokens.clear();
                config.temperature = 0.8f;

                // CRITICAL: Ensure language is NOT set for 'tiny.en'
                // config.language = "en"; 

                // 5. DEBUG: Gen Start
                m_fullTranscript << "[Gen] ";

                auto result = m_pipeline->generate(chunk.audioData, config);

                if (!result.texts.empty()) {
                    std::string text = result.texts[0];
                    if (!text.empty()) {
                        // SUCCESS
                        m_fullTranscript << " >>> " << text << " <<< ";
                    }
                    else {
                        m_fullTranscript << "[EmptyStr] ";
                    }
					m_fullTranscript << " >>> " << text << " <<< ";
                }
                else {
                    m_fullTranscript << "[NoRes] ";
                }
            }
            catch (const std::exception& e) {
                m_fullTranscript << "[Err: " << e.what() << "] ";
            }
        }
        else if (!m_pipeline) {
            m_fullTranscript << "[NoPipe] ";
        }

        if (chunk.isLastChunk) {
            m_isRunning = false;
            m_fullTranscript << "[End]";
        }
    }
    return m_fullTranscript.str();
}