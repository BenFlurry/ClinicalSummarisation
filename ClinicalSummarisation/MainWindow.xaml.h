#pragma once

#include "MainWindow.g.h"

// Include your custom classes
#include "AudioRecorder.h"
#include "TranscriptionEngine.h"
#include "AudioTranscriptionBridge.h"

namespace winrt::ClinicalSummarisation::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void startRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void stopRecording_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        // 1. The Bridge (The queue that connects them)
        AudioTranscriptionBridge m_bridge;

        // 2. The Components (Pointers so we can delete/re-create them)
        AudioRecorder* m_recorder = nullptr;
        TranscriptionEngine* m_engine = nullptr;

        // 3. The Background Thread
        std::thread m_processingThread;
    };
}

namespace winrt::ClinicalSummarisation::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
