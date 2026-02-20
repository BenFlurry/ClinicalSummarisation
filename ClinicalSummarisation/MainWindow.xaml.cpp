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
        titleBar.ForegroundColor(white); 

        // button colours
        titleBar.ButtonBackgroundColor(black);
        titleBar.ButtonForegroundColor(white);

        // button bover colours
        titleBar.ButtonHoverBackgroundColor(darkGray);
        titleBar.ButtonHoverForegroundColor(white);

        // button pressed colours
        titleBar.ButtonPressedBackgroundColor(white);
        titleBar.ButtonPressedForegroundColor(black);

        // inactive button colours
        titleBar.InactiveBackgroundColor(black);
        titleBar.InactiveForegroundColor(darkGray);
        titleBar.ButtonInactiveBackgroundColor(black);
        titleBar.ButtonInactiveForegroundColor(darkGray);

        MainWindow::SetAppState(AppState::Loading);

        m_summariser = new SummarisationEngine();

        // thread to start loading transcription and recording classes
        std::thread initThread([this]() {
            m_recorder = new AudioRecorder(&m_bridge);
            m_engine = new TranscriptionEngine(&m_bridge);
            m_engine->InitialiseModel();

            // once loaded, we can allow the user to record
            MainWindow::SetAppState(AppState::WaitingRecording);

            // start loading the heavy med42 model in parallel
            m_summariserLoadFuture = std::async(std::launch::async, [this]() {
                try {
                    m_summariser->loadModel(); 
                    m_isSummariserReady = true; 
                }
                catch (...) {
                    // device is incompatible if the model doesn't load
                    MainWindow::SetAppState(AppState::IncompatibleDevice);
                }
                });
            });
        initThread.detach();
    }

    // when the user presses start recording
    void MainWindow::startRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        MainWindow::SetAppState(AppState::Recording);

        // get our selected mic and find its name
        auto selectedMic = MicComboBox().SelectedItem();
        winrt::hstring winrtMicName = winrt::unbox_value<winrt::hstring>(selectedMic);
        std::string micName = winrt::to_string(winrtMicName);

        m_recorder->SetMicrophoneName(micName);

        // rejoin processing thread if available
        if (m_processingThread.joinable()) m_processingThread.join();

        // get our doctors speach embedding
        std::vector<float> doctorProfile = m_doctorEmbedding.getSpeachEmbedding();
        m_engine->SetDoctorProfile(doctorProfile);

        // start processing thread
        m_processingThread = std::thread([this]() {
            // this blocks until recording stops
            std::string finalTranscript = m_engine->ProcessLoop();

            // once recording has completed if med42 hasnt loaded
            if (!m_isSummariserReady) {
                this->DispatcherQueue().TryEnqueue([this]() {
                    StatusText().Text(L"Loading Summariser");
                    StatusSpinner().Visibility(Visibility::Visible);
                    });
                // wait for background loading of med42 to complete
                if (m_summariserLoadFuture.valid()) m_summariserLoadFuture.wait();
            }

            std::string soapNote = "";
            bool success = true;
            try {
                soapNote = m_summariser->generateTranscription(finalTranscript);
            }
            catch (...) {
                // if trying to prompt the model fails then device isnt compatible
                MainWindow::SetAppState(AppState::IncompatibleDevice);
                success = false;
            }

            // show summarisation
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

        // let processing run in parallel
        m_processingThread.detach();

        // start recording on current thread
        m_recorder->Start();
    }

    // when user finishes recording conversation
    void MainWindow::stopRecording_Click(IInspectable const&, RoutedEventArgs const&)
    {
        MainWindow::SetAppState(AppState::GeneratingSummarisation);

        // stop recording, flushing the last chunk of audio data into the bridge
        if (m_recorder) {
            m_recorder->Stop();
        }
    }

    // to copy summarisation to clipboard
    void MainWindow::copyButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // create window data package and add text from the editable UI summary box
        winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(MyTextBox().Text());

        // add to clip board
        winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);

        CopyButtonText().Text(L"Copied");

        // half a second show "copied" in place of "copy to clipboard"
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            this->DispatcherQueue().TryEnqueue([this]() {
                CopyButtonText().Text(L"Copy to Clipboard");
                });

            }).detach();
    }

    // get all possible microphones
    winrt::fire_and_forget MainWindow::loadMicrophones() {
        auto devices = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(
            winrt::Windows::Devices::Enumeration::DeviceClass::AudioCapture
        );
        // clear existing items
        MicComboBox().Items().Clear();

        if (devices.Size() == 0) {
            // placeholder if no mics available
            MicComboBox().Items().Append(winrt::box_value(L"No microphones found"));
        }
        else {
            // add to UI all found mics
            for (auto const& device : devices) {
                MicComboBox().Items().Append(winrt::box_value(device.Name()));
            }
            MicComboBox().SelectedIndex(0);
        }

    }


    // save summarisation to text file
    winrt::fire_and_forget MainWindow::saveSummarisation_Click(IInspectable const&, RoutedEventArgs const&) {
        try {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm;

            localtime_s(&now_tm, &now_c);

            std::wstringstream wss;
            wss << std::put_time(&now_tm, L"%Y-%m-%d-Summary-");

            winrt::Windows::Storage::Pickers::FileSavePicker savePicker;

            // initialise with windows handle
            auto initializeWithWindow = savePicker.as<::IInitializeWithWindow>();
            initializeWithWindow->Initialize(m_hWnd);

            savePicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            savePicker.FileTypeChoices().Insert(L"Text File", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
            savePicker.SuggestedFileName(wss.str());

            // show windows file system 
            winrt::Windows::Storage::StorageFile file = co_await savePicker.PickSaveFileAsync();

            if (file) {
                winrt::hstring content = winrt::to_hstring(m_summarisation);

                // prevent empty file errors if box is empty
                if (content.empty()) content = L"No summarisation available.";

                // write to file asynchronously
                co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, content);

            }
            else {
                StatusText().Text(L"Save Cancelled");
            }
        }
        catch (winrt::hresult_error const& ex) {
            // catch windows errors
            StatusText().Text(L"Save Failed: " + ex.message());
        }
        catch (std::exception const& ex) {
            // catch c++ errors
            StatusText().Text(L"Error: " + winrt::to_hstring(ex.what()));
        }
        catch (...) {
            // other errors
            StatusText().Text(L"An unknown error occurred while saving.");
        }
    }

    winrt::fire_and_forget MainWindow::saveTranscription_Click(IInspectable const&, RoutedEventArgs const&) {
        try {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm;

            localtime_s(&now_tm, &now_c);

            std::wstringstream wss;
            wss << std::put_time(&now_tm, L"%Y-%m-%d-Transcript-");

            winrt::Windows::Storage::Pickers::FileSavePicker savePicker;

            // initialise with window handle
            auto initializeWithWindow = savePicker.as<::IInitializeWithWindow>();
            initializeWithWindow->Initialize(m_hWnd);

            savePicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            savePicker.FileTypeChoices().Insert(L"Text File", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
            savePicker.SuggestedFileName(wss.str());

            // show windows file system
            winrt::Windows::Storage::StorageFile file = co_await savePicker.PickSaveFileAsync();

            if (file) {
                winrt::hstring content = winrt::to_hstring(m_transcription);

                // prevent empty file errors if box is empty
                if (content.empty()) content = L"No transcription available.";

                // write to file asynchronously
                co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, content);

            }
            else {
                StatusText().Text(L"Save Cancelled");
            }
        }
        catch (winrt::hresult_error const& ex) {
            // windows specific errors
            StatusText().Text(L"Save Failed: " + ex.message());
        }
        catch (std::exception const& ex) {
            // standard c++ errors
            StatusText().Text(L"Error: " + winrt::to_hstring(ex.what()));
        }
        catch (...) {
            StatusText().Text(L"An unknown error occurred while saving.");
        }
    }

    // start doctor voice print 
    void MainWindow::enrollVoice_Click(IInspectable const&, RoutedEventArgs const&)
    {
        MainWindow::SetAppState(AppState::EnrollingVoice);

        // check models have loaded
        if (!m_engine || !m_engine->GetEncoder()) {
            StatusText().Text(L"Models still loading...");
            return;
        }

        // start encoding loop
        m_doctorEmbedding.EnrollNewSpeakerAsync(m_engine->GetEncoder());
    }

    // finish doctor voice print
    void MainWindow::finishEnrollment_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_doctorEmbedding.FinishEnrollmentEarly();

        MainWindow::SetAppState(AppState::WaitingRecording);
        StatusText().Text(L"Voice Profile Saved Successfully.");
        // cant inject doctors profile into encoding engine yet as processing wouldnt have finished
    }

    // cancel enrollment
    void MainWindow::cancelEnrollment_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // abort
        m_doctorEmbedding.CancelEnrollment();
        MainWindow::SetAppState(AppState::WaitingRecording);
        StatusText().Text(L"Voice Enrollment Cancelled.");
    }

    // app state machine
    void MainWindow::SetAppState(AppState state) {
        this->DispatcherQueue().TryEnqueue([this, state]() {
            // default back to nothing
            StatusSpinner().Visibility(Visibility::Collapsed);
            ControlButtons().Visibility(Visibility::Collapsed);
            EnrollmentPanel().Visibility(Visibility::Collapsed);
            enrollVoice_btn().IsEnabled(false);
            PostSummarisationButtons ().Visibility(Visibility::Collapsed);
            MyTextBox().Visibility(Visibility::Collapsed);
            copy_btn().Visibility(Visibility::Collapsed);

            startRecording_btn().IsEnabled(false);
            stopRecording_btn().IsEnabled(false);

            // process each state and what should or shouldn't be shown
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
				ControlButtons().Visibility(Visibility::Visible);
				MyTextBox().Visibility(Visibility::Visible);
				copy_btn().Visibility(Visibility::Visible);
				saveTranscript_btn().Visibility(Visibility::Visible);
				startRecording_btn().IsEnabled(true);
                break;
            }
            });
    }
}


