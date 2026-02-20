#include "pch.h" 
#include "SpeakerEncoder.h"
#include <iostream>
#pragma warning(disable : 4996)
SpeakerEncoder::SpeakerEncoder() {}
SpeakerEncoder::~SpeakerEncoder() {}

void SpeakerEncoder::Initialize(const std::string& modelPath, const std::string& device) {
	// load the model
    is_loaded = false;
	m_model = m_core.compile_model(modelPath, device);
	m_request = m_model.create_infer_request();
    is_loaded = true;
}

// get the embedding from model
std::vector<float> SpeakerEncoder::GetEmbedding(const std::vector<float>& audioBuffer) {
    // resize for shape [1, Time]
    ov::Tensor inputTensor(ov::element::f32, { 1, audioBuffer.size() }, const_cast<float*>(audioBuffer.data()));

    // set input buffer to audioBuffer
    m_request.set_input_tensor(inputTensor);

    // run inference
    m_request.infer();

    // extract output from tensor
    const auto& outputTensor = m_request.get_output_tensor();
    const float* outputData = outputTensor.data<float>();
    size_t outputSize = outputTensor.get_size(); 
    return std::vector<float>(outputData, outputData + outputSize);
}

// calculate similarity between 2 voice prints
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