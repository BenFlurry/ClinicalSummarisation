#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <microsoft.ui.xaml.window.h> 
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Provider.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Data.Json.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <wrl/client.h>
#include <fstream>
#include <KnownFolders.h>

#pragma comment(lib, "shell32.lib")

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {

    MainWindow::MainWindow() {

        InitializeComponent();
        // load available mics
        MainWindow::loadMicrophones();
        MainWindow::InitialiseDatabase();

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
    void MainWindow::startRecording_Click(IInspectable const&, RoutedEventArgs const&) {
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
    void MainWindow::stopRecording_Click(IInspectable const&, RoutedEventArgs const&) {
        MainWindow::SetAppState(AppState::GeneratingSummarisation);

        // stop recording, flushing the last chunk of audio data into the bridge
        if (m_recorder) {
            m_recorder->Stop();
        }
    }

    // to copy summarisation to clipboard
    void MainWindow::copyButton_Click(IInspectable const&, RoutedEventArgs const&) {
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

    winrt::Windows::Foundation::IAsyncAction MainWindow::SaveTextToFileAsync(std::wstring suggestedFileName, winrt::hstring content) { 
        // Safer, winrt method of saving the text to file without the default file location

		//winrt::Windows::Storage::Pickers::FileSavePicker savePicker;

		//// initialise with window handle
		//auto initializeWithWindow = savePicker.as<::IInitializeWithWindow>();
		//initializeWithWindow->Initialize(m_hWnd);

		//savePicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
		//savePicker.FileTypeChoices().Insert(L"Text File", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
		//savePicker.SuggestedFileName(suggestedFileName);

		//// show windows file system 
		//winrt::Windows::Storage::StorageFile file = co_await savePicker.PickSaveFileAsync();

		//// write to file asynchronously
		//co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, content);

        // Hacky way that means it opens the file explorer at the folder of the default file location setting
        ::Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
        winrt::check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));

        // set starting folder from settings
        auto localSettings = winrt::Windows::Storage::ApplicationData::Current().LocalSettings();
        auto tokenBox = localSettings.Values().TryLookup(L"DefaultSaveToken");

        bool customFolderSet = false;

        if (tokenBox) {
            try {
                // get winrt token for saved folder
                winrt::hstring token = winrt::unbox_value<winrt::hstring>(tokenBox);
                winrt::Windows::Storage::StorageFolder savedFolder = co_await winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().GetFolderAsync(token);

                // convert to win32 shell item
                ::Microsoft::WRL::ComPtr<IShellItem> folderItem;
                if (SUCCEEDED(SHCreateItemFromParsingName(savedFolder.Path().c_str(), nullptr, IID_PPV_ARGS(&folderItem)))) {
                    // set dialog to open at this folder
                    dialog->SetFolder(folderItem.Get());
                    customFolderSet = true;
                }
            }
            catch (...) {
                // any errors opening this folder we pass and use the desktop as default
            }
        }

        if (!customFolderSet) {
            ::Microsoft::WRL::ComPtr<IShellItem> desktopItem;

            // FOLDERID_Desktop gets the exact path of the current user's desktop
            if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&desktopItem)))) {
                dialog->SetFolder(desktopItem.Get());
            }
        }

        // set file type to .txt
        COMDLG_FILTERSPEC spec[] = { { L"Text File", L"*.txt" } };
        dialog->SetFileTypes(1, spec);
        dialog->SetDefaultExtension(L"txt");

        // load suggested file name
        dialog->SetFileName(suggestedFileName.c_str());

        // show file explorer to user
        HRESULT handleResult = dialog->Show(m_hWnd);

        // if save is pressed
        if (SUCCEEDED(handleResult)) {
            ::Microsoft::WRL::ComPtr<IShellItem> resultItem;
            dialog->GetResult(&resultItem);

            PWSTR pszFilePath = nullptr;
            resultItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

            if (pszFilePath) {
                // write text to file
                std::string utf8Content = winrt::to_string(content);
                if (utf8Content.empty()) utf8Content = "No content available.";

                std::ofstream outFile(pszFilePath, std::ios::out | std::ios::binary);
                if (outFile.is_open()) {
                    outFile.write(utf8Content.c_str(), utf8Content.size());
                    outFile.close();
                }

                // free path string memory
                CoTaskMemFree(pszFilePath);
            }
        }
        // do nothing otherwise
    }

    std::wstring MainWindow::getFilenamePrefix(const wchar_t* formatString) {
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		std::time_t now_c = std::chrono::system_clock::to_time_t(now);
		std::tm now_tm;

		localtime_s(&now_tm, &now_c);

		std::wstringstream wss;
		wss << std::put_time(&now_tm, formatString);
        return wss.str();
    }

    // save summarisation to text file
    winrt::fire_and_forget MainWindow::saveSummarisation_Click(IInspectable const&, RoutedEventArgs const&) {
		std::wstring filenamePrefix = MainWindow::getFilenamePrefix(L"%Y-%m-%d_Summary_");

        // grab the text from the text box rather than m_summarisation in case the doctor has edited the summary
		winrt::hstring content = MyTextBox().Text();
		if (content.empty()) content = L"No summarisation available.";

		MainWindow::SaveTextToFileAsync(filenamePrefix, content);
    }

    // save transcription to text file
    winrt::fire_and_forget MainWindow::saveTranscription_Click(IInspectable const&, RoutedEventArgs const&) {
		std::wstring filenamePrefix = MainWindow::getFilenamePrefix(L"%Y-%m-%d_Transcript_");

		winrt::hstring content = winrt::to_hstring(m_transcription);
		if (content.empty()) content = L"No transcription available.";

		MainWindow::SaveTextToFileAsync(filenamePrefix, content);
    }

    // start doctor voice print 
    void MainWindow::enrollVoice_Click(IInspectable const&, RoutedEventArgs const&) {
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
    void MainWindow::finishEnrollment_Click(IInspectable const&, RoutedEventArgs const&) {
        m_doctorEmbedding.FinishEnrollmentEarly();

        MainWindow::SetAppState(AppState::WaitingRecording);
        StatusText().Text(L"Voice Profile Saved Successfully.");
        // cant inject doctors profile into encoding engine yet as processing wouldnt have finished
    }

    // cancel enrollment
    void MainWindow::cancelEnrollment_Click(IInspectable const&, RoutedEventArgs const&) {
        // abort
        m_doctorEmbedding.CancelEnrollment();
        MainWindow::SetAppState(AppState::WaitingRecording);
        StatusText().Text(L"Voice Enrollment Cancelled.");
    }

    // handler to open appraisal form
    winrt::fire_and_forget MainWindow::createAppraisal_Click(IInspectable const&, RoutedEventArgs const&) {
        AppraisalSummaryBox().Text(winrt::to_hstring(m_summarisation));

        AppraisalNameBox().Text(L"Enter appraisal name");
        AppraisalNameBox().SelectionStart(AppraisalNameBox().Text().size());

        AppraisalTagsBox().Text(L"Enter tags e.g. flu, pregnancy");
        AppraisalNotesBox().Text(L"Enter appraisal notes");

        // bind the popup to the main window's visual tree
        AppraisalDialog().XamlRoot(this->Content().XamlRoot());

        // show dialog 
        co_await AppraisalDialog().ShowAsync();
    }

    // saving appraisal form 
    winrt::fire_and_forget MainWindow::appraisalDialog_SaveClick(IInspectable const&, RoutedEventArgs const&) {
        // extract text from boxes
        winrt::hstring name = AppraisalNameBox().Text();
        winrt::hstring summary = AppraisalSummaryBox().Text();
        winrt::hstring tags = AppraisalTagsBox().Text();
        winrt::hstring notes = AppraisalNotesBox().Text();


        AppraisalDialog().Hide();

        std::wstringstream txtContent;
        txtContent << L"Appraisal Name: " << std::wstring_view(name) << L"\n\n";
        txtContent << L"Tags: " << std::wstring_view(tags) << L"\n\n";
        txtContent << L"Summary:\n" << std::wstring_view(summary) << L"\n\n";
        txtContent << L"Notes:\n" << std::wstring_view(notes) << L"\n";

        // save to json and user placed text file
        std::wstring fileName = name.empty() ? L"Untitled_Appraisal" : std::wstring(name);

        // set .txt filename to have the YYYY-MM-DD_Appraisal_ prefix
        std::wstring filePrefix = MainWindow::getFilenamePrefix(L"%Y-%m-%d_Appraisal_");
        std::wstring fileName = filePrefix + (name.empty() ? L"Untitled" : std::wstring(name));

        co_await MainWindow::SaveTextToFileAsync(fileName, winrt::hstring(txtContent.str()));
        co_await MainWindow::SaveAppraisalToJsonAsync(name, summary, tags, notes);
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

    // adds the appraisal to the JSON file in AppData/App/LocalState for offline storage
    winrt::Windows::Foundation::IAsyncAction MainWindow::SaveAppraisalToJsonAsync(winrt::hstring name, winrt::hstring summary, winrt::hstring tags, winrt::hstring notes) {
        using namespace winrt::Windows::Storage;
        using namespace winrt::Windows::Data::Json;

        StorageFolder localFolder = ApplicationData::Current().LocalFolder();

        // get the json file with all the appraisals stored in the localstate section of app data for this application
        StorageFile file = co_await localFolder.GetFileAsync(L"appraisals.json");

        // read
        winrt::hstring jsonString = co_await FileIO::ReadTextAsync(file);
        JsonObject rootObject;

        if (!JsonObject::TryParse(jsonString, rootObject)) {
            // start fresh if file is corrupted
            rootObject = JsonObject(); 
        }

        // create data for this appraisal
        JsonObject appraisalData;

        // get the date and time
        appraisalData.SetNamedValue(L"summary", JsonValue::CreateStringValue(summary));
        appraisalData.SetNamedValue(L"notes", JsonValue::CreateStringValue(notes));

        std::wstring dateString = MainWindow::getFilenamePrefix(L"%Y-%m-%dT%H:%M:%S");
        appraisalData.SetNamedValue(L"date", JsonValue::CreateStringValue(dateString));

        // turn tags into json array
        JsonArray tagsArray;
        std::wstring tagsStr = tags.c_str();
        std::wstringstream ts(tagsStr);
        std::wstring token;

		// string trimming for tags
        while (std::getline(ts, token, L',')) {
            size_t first = token.find_first_not_of(L" ");
            size_t last = token.find_last_not_of(L" ");
            if (first != std::string::npos && last != std::string::npos) {
                token = token.substr(first, (last - first + 1));
                tagsArray.Append(JsonValue::CreateStringValue(token));
            }
        }

        // default to untagged if empty
        if (tagsArray.Size() == 0) tagsArray.Append(JsonValue::CreateStringValue(L"untagged"));

        appraisalData.SetNamedValue(L"tags", tagsArray);

        // save with appraisal name as the key
        winrt::hstring keyName = name.empty() ? L"Untitled_Appraisal" : name;
        rootObject.SetNamedValue(keyName, appraisalData);

        // write to disk
        co_await FileIO::WriteTextAsync(file, rootObject.Stringify());
        // TODO: what if appraisals have multiple names
    }    
    
    // discarding appraisal form
    void MainWindow::appraisalDialog_CancelClick(IInspectable const&, RoutedEventArgs const&) {
        AppraisalDialog().Hide();
    }

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

    winrt::Windows::Foundation::IAsyncAction MainWindow::SelectDefaultSaveLocationAsync()
    {
        winrt::Windows::Storage::Pickers::FolderPicker folderPicker;

        // Initialize with the window handle
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


