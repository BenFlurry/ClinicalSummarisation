#include "pch.h"
#include "AudioTranscriptionBridge.h"

// push onto the bridge
void AudioTranscriptionBridge::Push(AudioChunk chunk) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_queue.push(chunk);
	m_cv.notify_one();
}

// pop off the bridge
AudioChunk AudioTranscriptionBridge::Pop() {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_cv.wait(lock, [this] { return !m_queue.empty(); });

	AudioChunk chunk = m_queue.front();
	m_queue.pop();
	return chunk;
}
