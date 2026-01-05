#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// Include your logic header
#include "summarisation.hpp"
#include "AudioRecordingThread.h"
#include <thread> // Required for background execution
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
    }

    void MainWindow::startRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // 1. Update UI state
        StatusText().Text(L"Initializing AI Model... (This may take a moment)");
        startRecording_btn().IsEnabled(false);
        stopRecording_btn().IsEnabled(false); // Disable both while loading
        MyTextBox().Text(L""); // Clear previous text

        // 2. Initialize the Components
        if (m_recorder) delete m_recorder;
        if (m_engine) delete m_engine;

        // Create new instances connected by the SAME bridge
        m_recorder = new AudioRecorder(&m_bridge);
        m_engine = new TranscriptionEngine(&m_bridge);

        // 3. Load the Model (Blocking call, but fast if cached)
        // Ensure this string matches your actual folder structure!
        try {
            m_engine->InitialiseModel();
        }
        catch (const std::exception& e) {
            winrt::hstring uiMessage = winrt::to_hstring(e.what());
            StatusText().Text(uiMessage);
            startRecording_btn().IsEnabled(true);
            return;
        }

        // 4. Start the Consumer Thread (The AI)
        // We start this BEFORE the recorder so it's ready to catch the first chunk
        if (m_processingThread.joinable()) m_processingThread.join();

        m_processingThread = std::thread([this]() {

            // This line BLOCKS until the user clicks Stop and the loop finishes
            std::string finalTranscript = m_engine->ProcessLoop();

            // --- UI UPDATE ON MAIN THREAD ---
            this->DispatcherQueue().TryEnqueue([this, finalTranscript]() {
                // Show the result
                MyTextBox().Text(to_hstring(finalTranscript));

                // Reset UI controls
                StatusText().Text(L"Transcription Complete.");
                startRecording_btn().IsEnabled(true);
                stopRecording_btn().IsEnabled(false);
                });
            });
        m_processingThread.detach(); // Let it run in background

        // 5. Start the Producer (The Mic)
        m_recorder->Start();

        // 6. Final UI Update
        StatusText().Text(L"Recording... Speak now!");
        stopRecording_btn().IsEnabled(true);
    }

    void MainWindow::stopRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StatusText().Text(L"Finalizing... Please wait.");
        startRecording_btn().IsEnabled(false);
        stopRecording_btn().IsEnabled(false);

        // 1. Stop the Recorder
        // This triggers the 'Flush' logic we wrote -> sends 'isLastChunk=true'
        if (m_recorder) {
            m_recorder->Stop();
        }

        // The UI will be re-enabled by the thread when it finishes processing!
    }
}
