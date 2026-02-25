#pragma once

#include "MainWindow.g.h"

// 1. Standard C++ Headers (Fixes std::thread, std::future, std::string, etc.)
#include <string>
#include <thread>
#include <future>
#include <atomic>
#include <vector>
#include <set>
#include <string>

// 2. Windows Headers (Fixes HWND)
#include <windows.h>

// 3. Your Custom Class Headers (Fixes AudioRecorder, Engines, etc.)
// Make sure these filenames match exactly what is in your Solution Explorer
#include "AudioStructures.h"
#include "AudioRecorder.h"
#include "TranscriptionEngine.h"  
#include "SummarisationEngine.h"   
#include "DoctorEmbedding.h"

namespace winrt::ClinicalSummarisation::implementation {
    enum class AppState {
        Loading,
        EnrollingVoice,
        WaitingRecording,
        Recording,
        IncompatibleDevice,
        GeneratingSummarisation,
        SummarisationComplete,
        AppraisalsView,
    };

    struct AppraisalItem {
        winrt::hstring Name;
        winrt::hstring Date;
        winrt::hstring Summary;
        winrt::hstring Notes;
        std::vector<std::wstring> Tags;
    };

    struct MainWindow : MainWindowT<MainWindow> {
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

        void appraisalHistory_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FilterSearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void CloseAppraisals_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHistoryItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget HistoryDialog_SaveClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HistoryDialog_CancelClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RenderAppraisalsList(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void RenderAppraisalsList();

        winrt::fire_and_forget saveTranscription_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget appraisalDialog_SaveClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget saveSummarisation_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget createAppraisal_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::fire_and_forget changeSaveLocation_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

        

    private:
        void SetAppState(AppState state);

        void InitialiseApplication();
        void InitialiseUI();
        void InitialiseTranscription();

        std::wstring getFilenamePrefix(const wchar_t* formatString);
        winrt::Windows::Foundation::IAsyncAction SaveTextToFileAsync(std::wstring suggestedFileName, winrt::hstring content);
        winrt::Windows::Foundation::IAsyncAction SaveAppraisalToJsonAsync(winrt::hstring name, winrt::hstring summary, winrt::hstring tags, winrt::hstring notes);
        winrt::Windows::Foundation::IAsyncAction SelectDefaultSaveLocationAsync();
        winrt::fire_and_forget InitialiseDatabase();
        winrt::fire_and_forget loadMicrophones();

        winrt::fire_and_forget LoadAppraisalsAsync();

        std::string m_originalAppraisalName;

        void PopulateTagFilterList(std::wstring searchQuery = L"");

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

        std::vector<AppraisalItem> m_allAppraisals;
        std::set<std::wstring> m_activeFilters;
        bool m_sortByName = false; 
    };
}

// required for winui3
namespace winrt::ClinicalSummarisation::factory_implementation {
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> { };
}
