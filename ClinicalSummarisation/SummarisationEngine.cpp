#include "pch.h"
#include "SummarisationEngine.h"
#include "Helpers.h"
#include <filesystem>

void SummarisationEngine::loadModel() {
    std::filesystem::path modelPath = Helpers::GetModelPath("Med42-int4");
    m_model = new ov::genai::LLMPipeline(modelPath, "GPU");
}

std::string SummarisationEngine::generateTranscription(std::string transcript) {
    std::string systemPrompt =
        "<|system|>\n"
        "You are an expert Clinical Medical Scribe. \n"
        "Task: Convert the following raw transcript into a professional SOAP Note (Subjective, Objective, Assessment, Plan) and a clinical summarisation of the text\n"
        "Rules:\n"
        "- Use the Speaker Labels (Doctor/Patient) to determine context.\n"
        "- Correct minor grammar mistakes but keep medical facts exact.\n"
        "- Output ONLY the SOAP note and summarisation. Do not chat.\n";

    std::string userPrompt =
        "<|user|>\n"
        "TRANSCRIPT:\n" + transcript + "\n"
        "<|assistant|>\n"
        "SOAP NOTE:\n"
        "SUMMARISATION: \n";

    std::string fullPrompt = systemPrompt + userPrompt;

    ov::genai::GenerationConfig config;
    config.max_new_tokens = 1024; // Allow long summaries
    config.temperature = 0.2f;    // Low temperature = More factual/consistent

    ov::genai::DecodedResults res = m_model->generate(fullPrompt, config);

    return res.texts[0];
}
