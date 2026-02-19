#include "pch.h" 
#include "SpeakerEncoder.h"
#include <iostream>
#pragma warning(disable : 4996)
SpeakerEncoder::SpeakerEncoder() {}
SpeakerEncoder::~SpeakerEncoder() {}

void SpeakerEncoder::Initialize(const std::string& modelPath, const std::string& device) {
	// Load the model
    is_loaded = false;
	m_model = m_core.compile_model(modelPath, device);
	m_request = m_model.create_infer_request();
    is_loaded = true;
}

std::vector<float> SpeakerEncoder::GetEmbedding(const std::vector<float>& audioBuffer) {
    //if (!is_loaded || audioBuffer.empty()) return {};

    // ECAPA-TDNN expects shape [1, Time]
    // We must resize the input port to match the audio length dynamically
    ov::Tensor inputTensor(ov::element::f32, { 1, audioBuffer.size() }, const_cast<float*>(audioBuffer.data()));

    // Set input (OpenVINO now reads directly from your 'audioBuffer')
    m_request.set_input_tensor(inputTensor);

    // 2. Run Inference
    m_request.infer();

    // 3. Extract Output
    // The output is usually a 1x192 embedding vector
    const auto& outputTensor = m_request.get_output_tensor();
    const float* outputData = outputTensor.data<float>();
    size_t outputSize = outputTensor.get_size(); // Should be 192

    // Copy to std::vector
    return std::vector<float>(outputData, outputData + outputSize);
}

float SpeakerEncoder::CosineSimilarity(const std::vector<float>& vecA, const std::vector<float>& vecB) {
    if (vecA.size() != vecB.size() || vecA.empty()) return 0.0f;

    float dot = 0.0f;
    float denomA = 0.0f;
    float denomB = 0.0f;

    for (size_t i = 0; i < vecA.size(); ++i) {
        dot += vecA[i] * vecB[i];
        denomA += vecA[i] * vecA[i];
        denomB += vecB[i] * vecB[i];
    }

    if (denomA == 0 || denomB == 0) return 0.0f;
    return dot / (std::sqrt(denomA) * std::sqrt(denomB));
}