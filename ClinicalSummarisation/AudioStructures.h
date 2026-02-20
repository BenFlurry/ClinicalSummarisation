#pragma once
#include <vector>
#include <string>

// holds a 30 second chunk of audio data with a flag to signal if it is the last
struct AudioChunk {
    std::vector<float> audioData; 
    bool isLastChunk = false;    
};