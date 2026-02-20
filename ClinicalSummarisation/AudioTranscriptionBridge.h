#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "AudioStructures.h"

// bridge between audio recording and transcription as a queue
class AudioTranscriptionBridge {
public:
	void Push(AudioChunk chunk);
	AudioChunk Pop();

private:
	std::queue<AudioChunk> m_queue;
	std::mutex m_mutex;
	std::condition_variable m_cv;
};

