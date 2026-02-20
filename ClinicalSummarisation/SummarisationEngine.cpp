#include "pch.h"
#include "SummarisationEngine.h"
#include "Helpers.h"
#include <filesystem>

// load med42 model (may take up to 1 minute for GPU)
void SummarisationEngine::loadModel() {
    std::filesystem::path modelPath = Helpers::GetModelPath("Med42-int4");
    m_model = new ov::genai::LLMPipeline(modelPath, "GPU");
}

// prompt med42 with transcript to create summary
std::string SummarisationEngine::generateTranscription(std::string transcript) {
    std::string systemPrompt =
        "<|system|>\n"
        "You are an expert Clinical Medical Summariser for the NHS in England. Your task is to document a formal History of Present Illness (HPI) based on the doctor-patient transcript.\n"
        "Rules for Documentation:\n"
        "1. **Style**: Write in a formal, objective clinical style (Third Person). Do not tell a story.\n"
        "2. **Attribution**: Attribute facts to the patient (e.g., use 'The patient stated...', 'She reports...', 'Patient notes...').\n"
        "3. **Detail**: Capture specific causes of injury, specific dates (convert spoken dates to DD/MM/YYYY), and specific names of providers if mentioned.\n"
        "4. **Chronology**: Present the history chronologically, starting with the initial onset/injury.\n"
        "5. **Content**: Include onset, duration, character of pain, aggravating factors, prior treatments, and recent exacerbations.\n"
        "\n"
        "Output Requirements:\n"
        "- Correct minor grammar mistakes but keep medical facts exact.\n"
        "- Output ONLY the final clinical note text. Do not chat or add introductory text.\n"
        "- Keep summarisation as breif as possible, ignore non-medical conversation, do not make assumptions on anything not explicitly mentioned in the transcript\n"
        "- Do not infer dates or activities not present in the text.\n";
        "- Do not provide any guidance, only the summary.\n";

    std::string userPrompt =
        "<|user|>\n"
        "TRANSCRIPT:\n" + transcript + "\n"
        "<|assistant|>\n"
        "SUMMARISATION: \n";

    std::string fullPrompt = systemPrompt + userPrompt;

    ov::genai::GenerationConfig config;
    config.max_new_tokens = 1024; 
    config.temperature = 0.2f;   

    ov::genai::DecodedResults res = m_model->generate(fullPrompt, config);

    return res.texts[0];
}
