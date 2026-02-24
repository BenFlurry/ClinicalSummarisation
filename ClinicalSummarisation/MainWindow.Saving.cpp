#include "pch.h"
#include "MainWindow.xaml.h"

// for clip board and data packages
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.AccessCache.h>

// for file io and pickers
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Provider.h>

// Win32 shell and COM headers
#include <shobjidl.h>
#include <shlobj.h> 
#include <KnownFolders.h>
#include <wrl/client.h> 
#pragma comment(lib, "shell32.lib")

// std library
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>


using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {
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
}