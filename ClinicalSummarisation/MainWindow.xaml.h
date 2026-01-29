#pragma once

#include "MainWindow.g.h"

// Include your custom classes
#include "AudioRecorder.h"
#include "TranscriptionEngine.h"
#include "AudioTranscriptionBridge.h"
#include "SummarisationEngine.h"
#include <future>
#include <thread>
#include <atomic>

namespace winrt::ClinicalSummarisation::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void startRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void stopRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void copyButton_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        AudioTranscriptionBridge m_bridge;
        AudioRecorder* m_recorder = nullptr;
        TranscriptionEngine* m_engine = nullptr;
        SummarisationEngine* m_summariser = nullptr;

        std::thread m_processingThread;

        // track background LLM loading
        std::future<void> m_summariserLoadFuture;
        std::atomic<bool> m_isSummariserReady{ false };
    };
}

namespace winrt::ClinicalSummarisation::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
