#pragma once
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include <cmath>

class SpeakerEncoder {
public:
    SpeakerEncoder();
    ~SpeakerEncoder();

    void Initialize(const std::string& modelPath, const std::string& device = "CPU");
    std::vector<float> GetEmbedding(const std::vector<float>& audioBuffer);

    // Helper to compare two fingerprints (0.0 to 1.0 score)
    static float CosineSimilarity(const std::vector<float>& vecA, const std::vector<float>& vecB);

    bool is_loaded;

private:
    ov::Core m_core;
    ov::CompiledModel m_model;
    ov::InferRequest m_request;
};
