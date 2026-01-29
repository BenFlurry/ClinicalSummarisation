#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <winrt/Windows.Storage.h>
#include <microsoft.ui.xaml.window.h> 
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        // background
        SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());


        // window size
        auto windowNative{ this->try_as<::IWindowNative>() };
        HWND hWnd{ 0 };
        windowNative->get_WindowHandle(&hWnd);

        winrt::Microsoft::UI::WindowId windowId;
        windowId.Value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hWnd));
        winrt::Microsoft::UI::Windowing::AppWindow appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);

        appWindow.Resize({ 1200,  800}); 

        // window title bar
        auto black = winrt::Windows::UI::ColorHelper::FromArgb(255, 0, 0, 0);
        auto white = winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255);
        auto darkGray = winrt::Windows::UI::ColorHelper::FromArgb(255, 50, 50, 50);

        auto titleBar = appWindow.TitleBar();
        titleBar.BackgroundColor(black);
        titleBar.ForegroundColor(white); // Text title color

        // Button Colors (Minimize, Maximize, Close)
        titleBar.ButtonBackgroundColor(black);
        titleBar.ButtonForegroundColor(white);

        // Button Hover State (When you mouse over them)
        titleBar.ButtonHoverBackgroundColor(darkGray);
        titleBar.ButtonHoverForegroundColor(white);

        // Button Pressed State (When you click down)
        titleBar.ButtonPressedBackgroundColor(white);
        titleBar.ButtonPressedForegroundColor(black);

        // 3. Set "Inactive" colors (When you click a different app)
        // It's good practice to make it slightly lighter so the user knows it's not focused
        titleBar.InactiveBackgroundColor(black);
        titleBar.InactiveForegroundColor(darkGray);
        titleBar.ButtonInactiveBackgroundColor(black);
        titleBar.ButtonInactiveForegroundColor(darkGray);

        StatusText().Text(L"Loading Transcription Models");
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
                StatusText().Text(L"Ready to Record");
                ControlButtons().Visibility(Visibility::Visible);
                startRecording_btn().IsEnabled(true);
                });

            // 3. Start Loading the Heavy LLM (Med42) in parallel
            m_summariserLoadFuture = std::async(std::launch::async, [this]() {
                try {
                    m_summariser->loadModel(); // <--- THIS WAS MISSING
                    m_isSummariserReady = true; // <--- Set the flag

                }
                catch (...) {
                }
                });
            });
        initThread.detach();
    }

    void MainWindow::startRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StatusText().Text(L"Listening to Conversation"); 
        MyTextBox().Visibility(Visibility::Collapsed); 
        copy_btn().Visibility(Visibility::Collapsed); 
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
                    StatusText().Text(L"Loading Summariser");
                    });
                // Wait for the background loader to finish
                if (m_summariserLoadFuture.valid()) m_summariserLoadFuture.wait();
            }

            std::string soapNote = "";
            bool success = true;
            try {
                soapNote = m_summariser->generateTranscription(finalTranscript);

            }
            catch (...) {
                success = false;

                this->DispatcherQueue().TryEnqueue([this]() {
                StatusText().Text(L"This device is not compatible with Intel's machine learning framework");
                ControlButtons().Visibility(Visibility::Collapsed);
                MyTextBox().Visibility(Visibility::Collapsed);
                copy_btn().Visibility(Visibility::Collapsed);
                startRecording_btn().IsEnabled(false);
                stopRecording_btn().IsEnabled(false);
                    });
            }

            if (success) {
                this->DispatcherQueue().TryEnqueue([this, soapNote, finalTranscript]() {
                    std::string fullOutput = soapNote;
                    MyTextBox().Text(to_hstring(fullOutput));
                    MyTextBox().Visibility(Visibility::Visible);
                    copy_btn().Visibility(Visibility::Visible);
                    StatusText().Text(L"Process Complete");
                    startRecording_btn().IsEnabled(true);
                    stopRecording_btn().IsEnabled(false);
				});
            }
		});

        // FIX: Do NOT detach inside the thread. Detach here.
        m_processingThread.detach();

        // FIX: Start the Recorder HERE (Outside the thread)
        // If you put this inside the thread, it never runs because ProcessLoop blocks first.
        m_recorder->Start();
    }
    void MainWindow::stopRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StatusText().Text(L"Generating Summarisation");
        startRecording_btn().IsEnabled(false);
        stopRecording_btn().IsEnabled(false);

        // 1. Stop the Recorder
        // This triggers the 'Flush' logic we wrote -> sends 'isLastChunk=true'
        if (m_recorder) {
            m_recorder->Stop();
        }
    }

    void MainWindow::copyButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // 1. Create a DataPackage (holds the content)
        winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;

        // 2. Put your text inside it
        package.SetText(MyTextBox().Text());

        // 3. Send it to the Clipboard
        winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);

        // (Optional) Change button text temporarily to show success
	    CopyButtonText().Text(L"Copied");

        std::thread([this]() {

            // Wait for 500 milliseconds
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // Jump back to the UI thread to reset the text
            // (You cannot touch UI elements directly from a background thread)
            this->DispatcherQueue().TryEnqueue([this]() {
                CopyButtonText().Text(L"Copy to Clipboard");
                });

            }).detach();
    }
}
