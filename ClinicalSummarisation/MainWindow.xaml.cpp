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
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Provider.h>
#include <shobjidl.h>
#include <iomanip>
#include <sstream>
#include <ctime>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        // load available mics
        MainWindow::loadMicrophones();

        // background
        SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());

        // window size
        auto windowNative{ this->try_as<::IWindowNative>() };
        winrt::check_hresult(windowNative->get_WindowHandle(&m_hWnd));


        winrt::Microsoft::UI::WindowId windowId;
        windowId.Value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m_hWnd));
        winrt::Microsoft::UI::Windowing::AppWindow appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);

        appWindow.Resize({ 1200,  800 });

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

        MainWindow::SetAppState(AppState::Loading);

        m_summariser = new SummarisationEngine();

        // Use a detached thread for the initialization sequence
        std::thread initThread([this]() {

            // 1. Load Critical Models (Whisper)
            m_recorder = new AudioRecorder(&m_bridge);
            m_engine = new TranscriptionEngine(&m_bridge);
            m_engine->InitialiseModel();

            // 2. Enable UI immediately (User can record now)
            MainWindow::SetAppState(AppState::WaitingRecording);

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
        MainWindow::SetAppState(AppState::Recording);
        auto selectedMic = MicComboBox().SelectedItem();

        winrt::hstring winrtMicName = winrt::unbox_value<winrt::hstring>(selectedMic);
        std::string micName = winrt::to_string(winrtMicName);

        m_recorder->SetMicrophoneName(micName);

        // FIX: Handle thread cleanup safely before starting a new one
        if (m_processingThread.joinable()) m_processingThread.join();

        std::vector<float> doctorProfile = m_doctorEmbedding.getSpeachEmbedding();
        m_engine->SetDoctorProfile(doctorProfile);

        // 1. Start the Consumer (Processing Thread)
        m_processingThread = std::thread([this]() {

            // This blocks until recording stops
            std::string finalTranscript = m_engine->ProcessLoop();

            // --- Post-Processing ---
            if (!m_isSummariserReady) {
                this->DispatcherQueue().TryEnqueue([this]() {
                    StatusText().Text(L"Loading Summariser");
                    StatusSpinner().Visibility(Visibility::Visible);
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
                MainWindow::SetAppState(AppState::IncompatibleDevice);
                success = false;
            }

            if (success) {
                MainWindow::SetAppState(AppState::SummarisationComplete);
                this->DispatcherQueue().TryEnqueue([this, soapNote, finalTranscript]() {
                    m_summarisation = soapNote;
                    m_transcription = finalTranscript;

                    std::string fullOutput = soapNote;
                    MyTextBox().Text(to_hstring(fullOutput));
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
        MainWindow::SetAppState(AppState::GeneratingSummarisation);

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

    winrt::fire_and_forget MainWindow::loadMicrophones() {
        auto devices = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(
            winrt::Windows::Devices::Enumeration::DeviceClass::AudioCapture
        );

        // 2. Switch back to the UI thread to update the ComboBox
        // (The 'co_await' automatically puts us back on the thread we started from)

        // Clear existing items
        MicComboBox().Items().Clear();

        if (devices.Size() == 0)
        {
            // Add a placeholder if nothing found
            MicComboBox().Items().Append(winrt::box_value(L"No microphones found"));
        }
        else
        {
            // 3. Loop through the collection
            for (auto const& device : devices)
            {
                // Create the ComboBoxItem content (The microphone name)
                MicComboBox().Items().Append(winrt::box_value(device.Name()));
            }

            // Select the first microphone by default
            MicComboBox().SelectedIndex(0);
        }

    }



    winrt::fire_and_forget MainWindow::saveSummarisation_Click(IInspectable const&, RoutedEventArgs const&)
    {
        try
        {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm;

            // 2. Convert to local time safely (Windows requires localtime_s)
            localtime_s(&now_tm, &now_c);

            // 3. Format the string (YYYY-MM-DD-Summary-)
            std::wstringstream wss;
            wss << std::put_time(&now_tm, L"%Y-%m-%d-Summary-");

            // 4. Set the suggested file name
            // 1. Create the FileSavePicker
            winrt::Windows::Storage::Pickers::FileSavePicker savePicker;

            // 2. IMPORTANT: Initialize it with your Window Handle (WinUI 3 Requirement)
            // If you skip this, the app will crash or the dialog won't show.
            auto initializeWithWindow = savePicker.as<::IInitializeWithWindow>();
            initializeWithWindow->Initialize(m_hWnd);

            // 3. Configure the Options
            savePicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            savePicker.FileTypeChoices().Insert(L"Text File", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
            // TODO
            savePicker.SuggestedFileName(wss.str());

            // 4. Show the Dialog and Wait for User Selection
            winrt::Windows::Storage::StorageFile file = co_await savePicker.PickSaveFileAsync();

            if (file)
            {
                // 5. User picked a file -> Write the content
                // We get the text from the UI box
                winrt::hstring content = winrt::to_hstring(m_summarisation);

                // Prevent empty file errors if box is empty
                if (content.empty()) content = L"No summarisation available.";

                // Write to file asynchronously
                co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, content);

                // Optional: Update UI to say "Saved!"
            }
            else
            {
                StatusText().Text(L"Save Cancelled");
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            // 1. Catch Windows-specific errors (Permissions, File Locked, etc.)
            // ex.message() returns a helpful localized string
            StatusText().Text(L"Save Failed: " + ex.message());
        }
        catch (std::exception const& ex)
        {
            // 2. Catch Standard C++ errors
            // Convert 'char*' to 'hstring'
            StatusText().Text(L"Error: " + winrt::to_hstring(ex.what()));
        }
        catch (...)
        {
            // 3. Catch anything else
            StatusText().Text(L"An unknown error occurred while saving.");
        }
    }

    winrt::fire_and_forget MainWindow::saveTranscription_Click(IInspectable const&, RoutedEventArgs const&)
    {
        try
        {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm;

            // 2. Convert to local time safely (Windows requires localtime_s)
            localtime_s(&now_tm, &now_c);

            // 3. Format the string (YYYY-MM-DD-Summary-)
            std::wstringstream wss;
            wss << std::put_time(&now_tm, L"%Y-%m-%d-Transcript-");

            // 4. Set the suggested file name
            // 1. Create the FileSavePicker
            winrt::Windows::Storage::Pickers::FileSavePicker savePicker;

            // 2. IMPORTANT: Initialize it with your Window Handle (WinUI 3 Requirement)
            // If you skip this, the app will crash or the dialog won't show.
            auto initializeWithWindow = savePicker.as<::IInitializeWithWindow>();
            initializeWithWindow->Initialize(m_hWnd);

            // 3. Configure the Options
            savePicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            savePicker.FileTypeChoices().Insert(L"Text File", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
            // TODO
            savePicker.SuggestedFileName(wss.str());

            // 4. Show the Dialog and Wait for User Selection
            winrt::Windows::Storage::StorageFile file = co_await savePicker.PickSaveFileAsync();

            if (file)
            {
                // 5. User picked a file -> Write the content
                // We get the text from the UI box
                winrt::hstring content = winrt::to_hstring(m_transcription);

                // Prevent empty file errors if box is empty
                if (content.empty()) content = L"No transcription available.";

                // Write to file asynchronously
                co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, content);

                // Optional: Update UI to say "Saved!"
            }
            else
            {
                StatusText().Text(L"Save Cancelled");
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            // 1. Catch Windows-specific errors (Permissions, File Locked, etc.)
            // ex.message() returns a helpful localized string
            StatusText().Text(L"Save Failed: " + ex.message());
        }
        catch (std::exception const& ex)
        {
            // 2. Catch Standard C++ errors
            // Convert 'char*' to 'hstring'
            StatusText().Text(L"Error: " + winrt::to_hstring(ex.what()));
        }
        catch (...)
        {
            // 3. Catch anything else
            StatusText().Text(L"An unknown error occurred while saving.");
        }
    }

    void MainWindow::enrollVoice_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // 1. Change UI state to show the reading prompt
        MainWindow::SetAppState(AppState::EnrollingVoice);

        // 2. Safety check: Ensure the AI models have finished loading
        if (!m_engine || !m_engine->GetEncoder()) {
            StatusText().Text(L"AI System is still loading. Please wait.");
            return;
        }

        // 3. Start the 30-second background recording loop
        m_doctorEmbedding.EnrollNewSpeakerAsync(m_engine->GetEncoder());
    }

    void MainWindow::finishEnrollment_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // 1. Tell the background loop to stop recording and process the audio
        m_doctorEmbedding.FinishEnrollmentEarly();

        // 2. Return the UI to the ready state
        MainWindow::SetAppState(AppState::WaitingRecording);
        StatusText().Text(L"Voice Profile Saved Successfully.");

        // Note: Because the saving happens on a background thread, we won't 
        // immediately inject the profile into the engine here to avoid a race condition.
        // We will load it right before the consultation starts instead.
    }

    void MainWindow::cancelEnrollment_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // 1. Tell the background loop to abort and delete the audio
        m_doctorEmbedding.CancelEnrollment();

        // 2. Return the UI to the ready state
        MainWindow::SetAppState(AppState::WaitingRecording);
        StatusText().Text(L"Voice Enrollment Cancelled.");
    }

    void MainWindow::SetAppState(AppState state) {
        this->DispatcherQueue().TryEnqueue([this, state]() {
            StatusSpinner().Visibility(Visibility::Collapsed);
            ControlButtons().Visibility(Visibility::Collapsed);
            EnrollmentPanel().Visibility(Visibility::Collapsed);
            enrollVoice_btn().IsEnabled(false);
            PostSummarisationButtons ().Visibility(Visibility::Collapsed);
            MyTextBox().Visibility(Visibility::Collapsed);
            copy_btn().Visibility(Visibility::Collapsed);

            startRecording_btn().IsEnabled(false);
            stopRecording_btn().IsEnabled(false);

            switch (state) {
            case AppState::Loading:
                StatusText().Text(L"Loading Application");
				StatusSpinner().Visibility(Visibility::Visible);
                break;

            case AppState::EnrollingVoice:
                StatusText().Text(L"Recording Voice Profile...");
                StatusSpinner().Visibility(Visibility::Visible);
                EnrollmentPanel().Visibility(Visibility::Visible);
                break;

            case AppState::WaitingRecording:
                StatusText().Text(L"Ready to Record");
                enrollVoice_btn().IsEnabled(true);
                ControlButtons().Visibility(Visibility::Visible);
                startRecording_btn().IsEnabled(true);
                break;

            case AppState::Recording:
                StatusText().Text(L"Listening to Conversation");
				StatusSpinner().Visibility(Visibility::Visible);
                ControlButtons().Visibility(Visibility::Visible);
				stopRecording_btn().IsEnabled(true);
                break;

            case AppState::IncompatibleDevice:
                StatusText().Text(L"This device is not compatible with Intel's machine learning framework");
                break;

            case AppState::GeneratingSummarisation:
                StatusText().Text(L"Generating Summarisation");
				StatusSpinner().Visibility(Visibility::Visible);
				startRecording_btn().IsEnabled(false);
				stopRecording_btn().IsEnabled(false);
                break;

            case AppState::SummarisationComplete:
                StatusText().Text(L"Clinical Summarisation");
				PostSummarisationButtons().Visibility(Visibility::Visible);
				MyTextBox().Visibility(Visibility::Visible);
				copy_btn().Visibility(Visibility::Visible);
				saveTranscript_btn().Visibility(Visibility::Visible);
				startRecording_btn().IsEnabled(true);
                break;
            }
            });
    }
}


