#pragma once
#include <vector>
#include <string>

struct AudioChunk {
    std::vector<float> audioData; // Raw audio samples
    bool isLastChunk = false;     // Flag to stop the consumer thread
};