#include "pch.h"
#include "summarisation.hpp"
#include <iostream>
#include <filesystem>
#include "openvino/genai/llm_pipeline.hpp"

// REQUIRED: Helper libraries to find the app folder
#include <winrt/Windows.ApplicationModel.h> 
#include <winrt/Windows.Storage.h>

std::string handle_go() {
    try {
        // get installation folder and convert to path
        auto packageFolder = winrt::Windows::ApplicationModel::Package::Current().InstalledLocation();
        std::filesystem::path packagePath = packageFolder.Path().c_str();
        std::filesystem::path fullModelPath = packagePath / "Med42-int4";

        ov::genai::LLMPipeline pipe(fullModelPath, "GPU");

        ov::genai::DecodedResults res = pipe.generate("What are the leading causes of type 1 diabetes");
        return res.texts[0];

    }
    catch (const std::exception& e) {
        std::cout << "CRASH ERROR: " << e.what() << std::endl;
        return e.what();
    }
    catch (...) {
        std::cout << "UNKNOWN CRASH happened!" << std::endl;
        return "unknown crash";
    }
}