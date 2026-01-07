#include "pch.h"
#include "TranscriptionEngine.h"
#include "Helpers.h"
#include <iostream>

TranscriptionEngine::TranscriptionEngine(AudioTranscriptionBridge* bridgePtr) {
	m_bridge = bridgePtr;
	m_isRunning = false;

    std::string encoderPath = Helpers::GetModelPath("speaker_encoder_int8/openvino_model.xml").string();
    m_speakerEncoder = new SpeakerEncoder();
    m_speakerEncoder->Initialize(encoderPath, "CPU");
}

TranscriptionEngine::~TranscriptionEngine() {
	if (m_pipeline) {
		delete m_pipeline;
	}
}

void TranscriptionEngine::InitialiseModel() {
	std::filesystem::path modelPath = Helpers::GetModelPath("whisper-medium-i4");
	m_pipeline = new ov::genai::WhisperPipeline(modelPath, "CPU");
}


std::string TranscriptionEngine::ProcessLoop() {
    // Clear previous text
    m_fullTranscript.str("");
    m_isRunning = true;

    m_fullTranscript << "[Start] ";

    while (m_isRunning) {
        AudioChunk chunk = m_bridge->Pop();
        //m_fullTranscript << "\n[New Chunk]";

        if (m_pipeline && !chunk.audioData.empty()) {

            float max_amp = 0.0f;
            for (float s : chunk.audioData) max_amp = std::max(max_amp, std::abs(s));

             //normalisation
            if (max_amp > 0.001f && max_amp < 0.9f) {
                float gain = 0.9f / max_amp;
                for (float& s : chunk.audioData) s *= gain;
            }

            // padding
            //size_t target_samples = 16000 * 30; 
            //if (chunk.audioData.size() < target_samples) {
            //    chunk.audioData.resize(target_samples, 0.0f);
            //}

            try {
                ov::genai::WhisperGenerationConfig config;
                config.max_new_tokens = 100;
                config.task = "transcribe";
                config.return_timestamps = true;
                config.num_beams = 3;

                auto result = m_pipeline->generate(chunk.audioData, config);
                std::vector<float>& sourceAudio = chunk.audioData;
                // Inside ProcessLoop... after Whisper returns 'result'
                if (result.chunks && !result.chunks->empty()) {

                    for (const auto& chunk : *result.chunks) {

                        // 1. Calculate Indices
                        // Whisper timestamps are in Seconds. Audio is 16000 Hz.
                        size_t startIdx = (size_t)(chunk.start_ts * 16000);
                        size_t endIdx = (size_t)(chunk.end_ts * 16000);

                        // Safety Bounds Check (Critical!)
                        if (startIdx >= sourceAudio.size()) startIdx = sourceAudio.size() - 1;
                        if (endIdx > sourceAudio.size()) endIdx = sourceAudio.size();
                        if (endIdx <= startIdx) continue; // Skip invalid clips

                        // 2. Extract Audio Clip
                        std::vector<float> clip(sourceAudio.begin() + startIdx, sourceAudio.begin() + endIdx);

                        // 3. Get Embedding (Only if clip is long enough, e.g., > 0.5s)
                        std::string speakerLabel = "[Unknown]";

                        if (clip.size() > 8000) { // > 0.5 seconds

                            float sumSquares = 0.0f;
                            for (float sample : clip) {
                                sumSquares += sample * sample;
                            }

                            float rms = std::sqrt(sumSquares / clip.size());
                            
                            m_fullTranscript << "RMS: " << std::to_string(rms) << "\n";


                            std::vector<float> currentEmbedding = m_speakerEncoder->GetEmbedding(clip);

                            // 4. Identify Speaker
                            if (m_doctorProfile.empty()) {
                                // First person to speak is assumed to be the Doctor
                                m_doctorProfile = currentEmbedding;
                                if (m_doctorProfile.empty()) {
                                    m_fullTranscript << "\nempty embedding\n";
                                }
                                speakerLabel = "Doctor [Anchor]";
                            }
                            else {
                                // Compare with Doctor
                                float score = SpeakerEncoder::CosineSimilarity(m_doctorProfile, currentEmbedding);
								std::string scoreStr = std::to_string(score).substr(0, 4); // "0.85"

                                // Threshold: usually 0.5 - 0.7 for ECAPA-TDNN
                                if (score > 0.75f) {
                                    speakerLabel = "Doctor: " + scoreStr;
                                }
                                else {
                                    speakerLabel = "Patient: " + scoreStr;
                                }
                            }
                        }

                        // 5. Output with Label
                        m_fullTranscript << "[" << speakerLabel << "] " << chunk.text << "\n";
                    }
                }

                if (!result.texts.empty()) {
                    std::string text = result.texts[0];

                    if (!text.empty()) {
                        // SUCCESS
                        for (const auto& segment : *result.chunks) {

                            float start = segment.start_ts;
                            float end = segment.end_ts;
                            std::string phrase = segment.text;

                            // 4. Format for the LLM (Text-Based Diarization)
                            // By adding [Time] + Newline, you help the LLM see "turns"
                            m_fullTranscript << "[" << std::fixed << std::setprecision(1) << start
                                << " - " << end << "] "
                                << phrase << "\n";
                        }
                    }
                    else {
                        m_fullTranscript << "[EmptyStr] ";
                    }
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