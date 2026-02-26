#include "pch.h"
#include "MainWindow.xaml.h"

// for storage and JSON
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Data.Json.h>

// for window sizing and UI
#include <microsoft.ui.xaml.window.h> 
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

// for colours
#include <winrt/Windows.UI.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {

    void MainWindow::InitialiseApplication() {
        MainWindow::loadMicrophones();
        MainWindow::InitialiseDatabase();
        MainWindow::InitialiseUI();
        MainWindow::InitialiseTranscription();
    }

    void MainWindow::InitialiseUI() {
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
    }

    void MainWindow::InitialiseTranscription() {
        m_summariser = new SummarisationEngine();
        // thread to start loading transcription and recording classes
        std::thread initThread([this]() {
            m_recorder = new AudioRecorder(&m_bridge);
            m_engine = new TranscriptionEngine(&m_bridge);
            m_engine->InitialiseModel();

            m_isDoctorEnrolled = m_doctorEmbedding.IsProfileEnrolled();

            if (m_isDoctorEnrolled) {
				MainWindow::SetAppState(AppState::WaitingRecording);
            }
            else {
                MainWindow::SetAppState(AppState::WaitingEnrollment);
            }
            // once loaded, we can allow the user to record

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


    // creates an empty JSON database for appraisals if it doesn't exist
    winrt::fire_and_forget MainWindow::InitialiseDatabase() {
        using namespace winrt::Windows::Storage;
        using namespace winrt::Windows::Data::Json;

        StorageFolder localFolder = ApplicationData::Current().LocalFolder();

        // check if file exists and return if so, otherwise create empty JSON object in file
        IStorageItem item = co_await localFolder.TryGetItemAsync(L"appraisals.json");
        if (item) co_return;

        StorageFile file = co_await localFolder.CreateFileAsync(L"appraisals.json", CreationCollisionOption::FailIfExists);
        JsonObject root;
        co_await FileIO::WriteTextAsync(file, root.Stringify());
    }
}
