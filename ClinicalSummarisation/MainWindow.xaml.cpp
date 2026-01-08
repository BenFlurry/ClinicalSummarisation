#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        StatusText().Text(L"Loading Transcription Models...");
        startRecording_btn().IsEnabled(false);
        stopRecording_btn().IsEnabled(false);

        m_summariser = new SummarisationEngine();

        // Use a detached thread for the initialization sequence
        std::thread initThread([this]() {

            // 1. Load Critical Models (Whisper)
            m_recorder = new AudioRecorder(&m_bridge);
            m_engine = new TranscriptionEngine(&m_bridge);
            m_engine->InitialiseModel();

            // 2. Enable UI immediately (User can record now)
            this->DispatcherQueue().TryEnqueue([this]() {
                StatusText().Text(L"Ready to Record (Loading Summariser in background...)");
                startRecording_btn().IsEnabled(true);
                });

            // 3. Start Loading the Heavy LLM (Med42) in parallel
            // FIX: actually call loadModel() here!
            m_summariserLoadFuture = std::async(std::launch::async, [this]() {
                try {
                    m_summariser->loadModel(); // <--- THIS WAS MISSING
                    m_isSummariserReady = true; // <--- Set the flag

                    this->DispatcherQueue().TryEnqueue([this]() {
                        // Only update text if user isn't already recording
                        if (startRecording_btn().IsEnabled()) {
                            StatusText().Text(L"Ready to Record (All Models Loaded)");
                        }
                        });
                }
                catch (...) {
                    // Handle load failure safely
                }
                });
            });
        initThread.detach();
    }

    void MainWindow::startRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StatusText().Text(L"Listening...");
        startRecording_btn().IsEnabled(false);
        stopRecording_btn().IsEnabled(true);
        MyTextBox().Text(L"");

        // FIX: Handle thread cleanup safely before starting a new one
        if (m_processingThread.joinable()) m_processingThread.join();

        // 1. Start the Consumer (Processing Thread)
        m_processingThread = std::thread([this]() {

            // This blocks until recording stops
            std::string finalTranscript = m_engine->ProcessLoop();

            // --- Post-Processing ---
            if (!m_isSummariserReady) {
                this->DispatcherQueue().TryEnqueue([this]() {
                    StatusText().Text(L"Waiting for summariser to load...");
                    });
                // Wait for the background loader to finish
                if (m_summariserLoadFuture.valid()) m_summariserLoadFuture.wait();
            }

            this->DispatcherQueue().TryEnqueue([this]() {
                StatusText().Text(L"Generating SOAP note...");
                });

            std::string soapNote = m_summariser->generateTranscription(finalTranscript);

            this->DispatcherQueue().TryEnqueue([this, soapNote]() {
                MyTextBox().Text(to_hstring(soapNote));
                StatusText().Text(L"Process Complete");
                startRecording_btn().IsEnabled(true);
                stopRecording_btn().IsEnabled(false);
                });
            });

        // FIX: Do NOT detach inside the thread. Detach here.
        m_processingThread.detach();

        // FIX: Start the Recorder HERE (Outside the thread)
        // If you put this inside the thread, it never runs because ProcessLoop blocks first.
        m_recorder->Start();
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
    }
}
