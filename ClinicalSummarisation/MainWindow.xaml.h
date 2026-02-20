#pragma once

#include "MainWindow.g.h"

#include "AudioRecorder.h"
#include "TranscriptionEngine.h"
#include "AudioTranscriptionBridge.h"
#include "SummarisationEngine.h"
#include "DoctorEmbedding.h"
#include <future>
#include <thread>
#include <atomic>

namespace winrt::ClinicalSummarisation::implementation
{
    enum class AppState {
        Loading,
        EnrollingVoice,
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

        // button handlers
        void startRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void stopRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void copyButton_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void enrollVoice_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void finishEnrollment_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void cancelEnrollment_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget saveTranscription_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget saveSummarisation_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget loadMicrophones();

        

    private:
        void SetAppState(AppState state);

        AudioTranscriptionBridge m_bridge;
        AudioRecorder* m_recorder = nullptr;
        TranscriptionEngine* m_engine = nullptr;
        SummarisationEngine* m_summariser = nullptr;
        DoctorEmbedding m_doctorEmbedding;

        std::thread m_processingThread;

        // track background LLM loading
        std::future<void> m_summariserLoadFuture;
        std::atomic<bool> m_isSummariserReady{ false };
        // for window resizing
        HWND m_hWnd{ 0 };
        
        std::string m_summarisation;
        std::string m_transcription;
    };
}

// required for winui3
namespace winrt::ClinicalSummarisation::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
