#include "pch.h"
#include "MainWindow.xaml.h"

// storage and cache access
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.AccessCache.h> 

// JSON
#include <winrt/Windows.Data.Json.h>

// string and time for string manipulation
#include <sstream>
#include <iomanip>
#include <ctime>

// UI controls
#include <winrt/Microsoft.UI.Xaml.Controls.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {

    // creates and shows the appraisal dialog
    winrt::fire_and_forget MainWindow::createAppraisal_Click(IInspectable const&, RoutedEventArgs const&) {
        // load summarisation into dialog
        AppraisalSummaryBox().Text(winrt::to_hstring(m_summarisation));

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
        std::wstring appraisalName = name.empty() ? L"" : std::wstring(name);

        // set .txt filename to have the YYYY-MM-DD_Appraisal_ prefix
        std::wstring filePrefix = MainWindow::getFilenamePrefix(L"%Y-%m-%d_Appraisal_");

        std::wstring fileName = filePrefix + appraisalName;

        co_await MainWindow::SaveTextToFileAsync(fileName, winrt::hstring(txtContent.str()));
        co_await MainWindow::SaveAppraisalToJsonAsync(name, summary, tags, notes);
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

}
