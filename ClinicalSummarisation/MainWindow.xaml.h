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
#include <winrt/Windows.Storage.Pickers.h>

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
        void appraisalDialog_CancelClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);        
        winrt::fire_and_forget saveTranscription_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget appraisalDialog_SaveClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget saveSummarisation_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget createAppraisal_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget changeSaveLocation_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        

    private:
        void SetAppState(AppState state);

        std::wstring getFilenamePrefix(const wchar_t* formatString);
        winrt::Windows::Foundation::IAsyncAction SaveTextToFileAsync(std::wstring suggestedFileName, winrt::hstring content);
        winrt::Windows::Foundation::IAsyncAction SelectDefaultSaveLocationAsync();
        winrt::fire_and_forget loadMicrophones();

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
