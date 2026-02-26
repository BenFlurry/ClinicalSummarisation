#include "pch.h"
#include "MainWindow.xaml.h"

// for microphone enumeration and search
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>

// for ApplicationData and StorageData
#include <winrt/Windows.Storage.h>

// for folder picker 
#include <winrt/Windows.Storage.Pickers.h>

// for FutureAccessList cache
#include <winrt/Windows.Storage.AccessCache.h>

// COM interface
#include <shobjidl.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {
    // start doctor voice print 
    void MainWindow::enrollVoice_Click(IInspectable const&, RoutedEventArgs const&) {
        MainWindow::SetAppState(AppState::EnrollingVoice);
        // start encoding loop
        m_doctorEmbedding.EnrollNewSpeakerAsync(m_engine->GetEncoder());
    }

    // finish doctor voice print
    void MainWindow::finishEnrollment_Click(IInspectable const&, RoutedEventArgs const&) {
        m_doctorEmbedding.FinishEnrollmentEarly();

        MainWindow::SetAppState(AppState::WaitingRecording);
        // cant inject doctors profile into encoding engine yet as processing wouldnt have finished
    }

    // cancel enrollment
    void MainWindow::cancelEnrollment_Click(IInspectable const&, RoutedEventArgs const&) {
        // abort
        m_doctorEmbedding.CancelEnrollment();
        m_isDoctorEnrolled = m_doctorEmbedding.IsProfileEnrolled();
        // if we dont have a profile and the enrollment cancelled then we wait enrollment
        if (m_isDoctorEnrolled) {
			MainWindow::SetAppState(AppState::WaitingRecording);
        }
        else {
			MainWindow::SetAppState(AppState::WaitingEnrollment);
        }
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

    // change the location where Appraisal, Summarisations and Transcriptions are stored
    winrt::fire_and_forget MainWindow::changeSaveLocation_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args) {
        // 1. Call the helper function to open the folder picker
        co_await MainWindow::SelectDefaultSaveLocationAsync();

        // 2. Retrieve the token we just saved to display the folder path in the UI
        auto localSettings = winrt::Windows::Storage::ApplicationData::Current().LocalSettings();
        auto tokenBox = localSettings.Values().TryLookup(L"DefaultSaveToken");

        if (tokenBox) {
            winrt::hstring token = winrt::unbox_value<winrt::hstring>(tokenBox);

            // Get the folder from the FutureAccessList using the token
            winrt::Windows::Storage::StorageFolder folder =
                co_await winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().GetFolderAsync(token);
        }

    }

    // helper function to change default storage location
    winrt::Windows::Foundation::IAsyncAction MainWindow::SelectDefaultSaveLocationAsync()
    {
        winrt::Windows::Storage::Pickers::FolderPicker folderPicker;

        // initialise with the window handle
        auto initializeWithWindow = folderPicker.as<::IInitializeWithWindow>();
        initializeWithWindow->Initialize(m_hWnd);

        folderPicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);

        // folder picker crashes if < 1 filter is applied so use an empty filter
        folderPicker.FileTypeFilter().Append(L"*");

        // show picker
        winrt::Windows::Storage::StorageFolder selectedFolder = co_await folderPicker.PickSingleFolderAsync();

        if (selectedFolder)
        {
            // add the folder to the FutureAccessList to keep write permissions
            winrt::hstring token = winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().Add(selectedFolder);

            // save to app local settings
            auto localSettings = winrt::Windows::Storage::ApplicationData::Current().LocalSettings();
            localSettings.Values().Insert(L"DefaultSaveToken", winrt::box_value(token));
        }
    }
}
