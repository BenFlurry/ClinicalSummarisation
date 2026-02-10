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
    enum class AppState {
        Loading,
        WaitingRecording,
        Recording,
        IncompatibleDevice,
        GeneratingSummarisation,
        SummarisationComplete,
        History
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
    public:
        MainWindow();

        void startRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void stopRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void copyButton_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget saveTranscription_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget loadMicrophones();

        

    private:
        void SetAppState(AppState state);

        AudioTranscriptionBridge m_bridge;
        AudioRecorder* m_recorder = nullptr;
        TranscriptionEngine* m_engine = nullptr;
        SummarisationEngine* m_summariser = nullptr;

        std::thread m_processingThread;

        // track background LLM loading
        std::future<void> m_summariserLoadFuture;
        std::atomic<bool> m_isSummariserReady{ false };
        HWND m_hWnd{ 0 };
    };
}

namespace winrt::ClinicalSummarisation::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
