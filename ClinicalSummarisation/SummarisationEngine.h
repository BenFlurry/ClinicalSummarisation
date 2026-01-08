#pragma once

#include "openvino/genai/llm_pipeline.hpp"
class SummarisationEngine {
private:
	ov::genai::LLMPipeline* m_model = nullptr;

public:
	void loadModel();
	std::string generateTranscription(std::string transcript);
};

