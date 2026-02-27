#include "pch.h"
#include "MainWindow.xaml.h"

#include <winrt/Windows.UI.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <algorithm>
#include <set>
#include <string_view> // Helper for string manipulation
#include <winrt/Microsoft.UI.Text.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Data::Json;

namespace winrt::ClinicalSummarisation::implementation {

    // 1. History Button Click
    void MainWindow::appraisalHistory_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        // Switch Views
        RecordingView().Visibility(Visibility::Collapsed);
        AppraisalsView().Visibility(Visibility::Visible);

        // Reset Search UI
        NameSearchBox().Text(L"");
        TagSearchBox().Text(L"");
        m_activeFilters.clear();

        // Load Data
        LoadAppraisalsAsync();
    }

    // 2. Load Data from JSON
    winrt::fire_and_forget MainWindow::LoadAppraisalsAsync() {
        m_allAppraisals.clear();
        try {
            auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            auto item = co_await localFolder.TryGetItemAsync(L"appraisals.json");
            if (!item) co_return;

            auto file = item.as<winrt::Windows::Storage::StorageFile>();
            auto jsonString = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            JsonObject root;

            if (JsonObject::TryParse(jsonString, root)) {
                for (auto const& pair : root) {
                    JsonObject val = pair.Value().GetObject();
                    AppraisalItem app;
                    app.Name = pair.Key();
                    app.Date = val.GetNamedString(L"date", L"");
                    app.Summary = val.GetNamedString(L"summary", L"");
                    app.Notes = val.GetNamedString(L"notes", L"");

                    JsonArray tagsArr = val.GetNamedArray(L"tags", JsonArray());
                    for (auto t : tagsArr) app.Tags.push_back(t.GetString().c_str());

                    m_allAppraisals.push_back(app);
                }
            }
        }
        catch (...) {}

        PopulateTagFilterList();

        // Call the VOID version (Manual update)
        RenderAppraisalsList();
    }

    // tag filter list
    void MainWindow::PopulateTagFilterList(std::wstring searchQuery) {
        TagFilterListView().Items().Clear();
        std::set<std::wstring> uniqueTags;
        for (auto const& app : m_allAppraisals) {
            for (auto const& t : app.Tags) uniqueTags.insert(t);
        }

        for (auto const& tag : uniqueTags) {
            if (!searchQuery.empty() && tag.find(searchQuery) == std::wstring::npos) continue;

            CheckBox cb;
            cb.Content(winrt::box_value(tag));
            cb.IsChecked(m_activeFilters.count(tag) > 0);

            cb.Checked([this, tag](auto&&...) { m_activeFilters.insert(tag); RenderAppraisalsList(); });
            cb.Unchecked([this, tag](auto&&...) { m_activeFilters.erase(tag); RenderAppraisalsList(); });

            TagFilterListView().Items().Append(cb);
        }
    }

    // renders appraisals grid
    void MainWindow::RenderAppraisalsList() {
        // 1. Clear the grid of appraisals
        AppraisalsListContainer().Items().Clear();

        std::wstring nameQuery = NameSearchBox().Text().c_str();

        // 2. Filter items to be displayed
        std::vector<AppraisalItem> filtered;
        for (auto const& app : m_allAppraisals) {
            // filter name
            if (!nameQuery.empty() && std::wstring_view(app.Name).find(nameQuery) == std::wstring::npos) continue;

            // filter tags
            if (!m_activeFilters.empty()) {
                bool match = false;
                for (auto const& t : app.Tags) if (m_activeFilters.count(t)) { match = true; break; }
                if (!match) continue;
            }
            filtered.push_back(app);
        }

        // 3. Sort by date (newest)
        std::sort(filtered.begin(), filtered.end(), [](const AppraisalItem& a, const AppraisalItem& b) {
            return a.Date > b.Date;
            });

        // 4. Generate Cards (The Glass Look)
        for (auto const& app : filtered) {

            // --- BUTTON WRAPPER ---
            Button itemButton;
            itemButton.Margin({ 6, 6, 6, 6 });
            itemButton.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            itemButton.VerticalContentAlignment(VerticalAlignment::Stretch);
            itemButton.Background(Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            itemButton.BorderThickness({ 0,0,0,0 });
            itemButton.Padding({ 0,0,0,0 });

            // Store ID for click handler
            itemButton.Tag(winrt::box_value(app.Name));
            itemButton.Click({ this, &MainWindow::OnHistoryItemClick });

            // --- THE GLASS BORDER ---
            Border glassBorder;
            glassBorder.Width(300); // Fixed width allows columns to form
            glassBorder.Padding({ 16, 16, 16, 16 });
            glassBorder.CornerRadius({ 20, 20, 20, 20 });

            // Semi-transparent white background (#66FFFFFF) and border (#B3FFFFFF)
            glassBorder.Background(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(102, 255, 255, 255)));
            glassBorder.BorderBrush(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(179, 255, 255, 255)));
            glassBorder.BorderThickness({ 1, 1, 1, 1 });

            // Container for text
            StackPanel card;
            card.Spacing(6);

            // --- TITLE ---
            TextBlock title;
            title.Text(app.Name);
            title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
            title.FontSize(16);
            title.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); // #0B3B60
            title.TextTrimming(TextTrimming::CharacterEllipsis);

            // --- DATE ---
            std::wstring_view dateView(app.Date);
            winrt::hstring dateStr = L"Date: ";
            winrt::hstring d = dateView.length() >= 10 ? winrt::hstring(dateView.substr(0, 10)) : app.Date;
            dateStr = dateStr + d;

            TextBlock date;
            date.Text(dateStr);
            date.FontSize(11);
            date.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); // #0B3B60
            date.Opacity(0.7);

            // --- TAGS ---
            std::wstring tagsJoined = L"Tags: ";
            for (size_t i = 0; i < app.Tags.size(); ++i) {
                tagsJoined += app.Tags[i];
                if (i < app.Tags.size() - 1) tagsJoined += L", ";
            }

            TextBlock tags;
            tags.Text(tagsJoined);
            tags.TextWrapping(TextWrapping::NoWrap);
            tags.TextTrimming(TextTrimming::CharacterEllipsis);
            tags.FontSize(11);
            tags.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
            tags.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); // #0B3B60

            // --- SUMMARY ---
            TextBlock summary;
            summary.Text(app.Summary);
            summary.TextWrapping(TextWrapping::Wrap);
            summary.TextTrimming(TextTrimming::CharacterEllipsis);
            summary.MaxHeight(60); // Prevents long text from stretching the tile
            summary.Margin({ 0, 4, 0, 0 });
            summary.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 30, 41, 59))); // #1E293B

            // Assemble the card
            card.Children().Append(title);
            card.Children().Append(date);
            card.Children().Append(tags);
            card.Children().Append(summary);

            glassBorder.Child(card);
            itemButton.Content(glassBorder);

            // Append to the GridView
            AppraisalsListContainer().Items().Append(itemButton);
        }
    }
    void MainWindow::OnHistoryItemClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        // 1. Recover the Name from the button's tag
        auto button = sender.as<Button>();
        auto nameBoxed = button.Tag();
        if (!nameBoxed) return;

        // This is the ID (Name) of the clicked item
        winrt::hstring appraisalName = winrt::unbox_value<winrt::hstring>(nameBoxed);

        // 2. Find the specific appraisal in our list
        AppraisalItem* foundItem = nullptr;
        for (auto& app : m_allAppraisals) {
            if (app.Name == appraisalName) {
                foundItem = &app;
                break;
            }
        }

        // 3. Populate the Dialog Fields
        if (foundItem) {
            HistoryNameBox().Text(foundItem->Name);
            HistorySummaryBox().Text(foundItem->Summary);
            HistoryNotesBox().Text(foundItem->Notes);

            // Convert the Vector of tags back into a String (e.g. "Tag1, Tag2")
            std::wstring tagsJoined;
            for (size_t i = 0; i < foundItem->Tags.size(); ++i) {
                tagsJoined += foundItem->Tags[i];
                if (i < foundItem->Tags.size() - 1) tagsJoined += L", ";
            }
            HistoryTagsBox().Text(winrt::hstring(tagsJoined));

            // 4. IMPORTANT: Store the Name so "Save" knows which item to update
            m_originalAppraisalName = winrt::to_string(foundItem->Name);

            // 5. Show the Dialog
            HistoryDialog().XamlRoot(this->Content().XamlRoot());
            HistoryDialog().ShowAsync();
        }
    }    // 5. EVENT HANDLER VERSION - Just calls the void version
    void MainWindow::RenderAppraisalsList(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
        RenderAppraisalsList();
    }

    // search for tags
    void MainWindow::FilterSearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
        PopulateTagFilterList(sender.as<TextBox>().Text().c_str());
    }

    // close appraisals
    void MainWindow::CloseAppraisals_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
        AppraisalsView().Visibility(Visibility::Collapsed);
        RecordingView().Visibility(Visibility::Visible);
    }

    winrt::fire_and_forget MainWindow::HistoryDialog_SaveClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
        // 1. Capture the new data from the UI
        winrt::hstring newName = HistoryNameBox().Text();
        winrt::hstring newSummary = HistorySummaryBox().Text();
        winrt::hstring newNotes = HistoryNotesBox().Text();
        winrt::hstring newTagsRaw = HistoryTagsBox().Text();

        HistoryDialog().Hide();

        // 2. Update the Internal List (Memory)
        // We look for the item with 'm_originalAppraisalName' and update it.
        // If the name changed, this logic effectively handles it.
        for (auto it = m_allAppraisals.begin(); it != m_allAppraisals.end(); ++it) {
            if (it->Name == winrt::to_hstring(m_originalAppraisalName)) {
                // Found the old entry! Update it.
                it->Name = newName;
                it->Summary = newSummary;
                it->Notes = newNotes;

                // Re-parse tags (simple comma split)
                it->Tags.clear();
                std::wstring_view tagView = newTagsRaw;
                size_t pos = 0;
                while ((pos = tagView.find(L',')) != std::wstring_view::npos) {
                    it->Tags.push_back(std::wstring(tagView.substr(0, pos)));
                    tagView = tagView.substr(pos + 1);
                    // Trim leading space (optional but good)
                    if (!tagView.empty() && tagView.front() == L' ') tagView.remove_prefix(1);
                }
                if (!tagView.empty()) it->Tags.push_back(std::wstring(tagView));

                break;
            }
        }

        // 3. Save the Internal List to JSON (So the app remembers changes next time)
        // You can reuse your existing SaveAppraisalToJsonAsync, 
        // OR simply rewrite the whole JSON file from 'm_allAppraisals' here.
        // For simplicity, let's assume you have a function to dump m_allAppraisals to disk.
        // co_await SaveAllAppraisalsToDiskAsync(); // <--- You'll need to implement this one day!

        // 4. Update the UI List
        MainWindow::RenderAppraisalsList();

        // 5. EXTERNAL SAVE: Launch the File Picker for the user
        // Construct the full text content
        std::wstringstream txtContent;
        txtContent << L"Appraisal: " << newName.c_str() << L"\n\n";
        txtContent << L"Summary:\n" << newSummary.c_str() << L"\n\n";
        txtContent << L"Notes:\n" << newNotes.c_str() << L"\n\n";
        txtContent << L"Tags: " << newTagsRaw.c_str();

        winrt::hstring fileContent = winrt::hstring(txtContent.str());

        // Hide dialog first so it doesn't block the picker

        // Reuse your existing helper to pop the explorer window
        std::wstring filePrefix = MainWindow::getFilenamePrefix(L"%Y-%m-%d_Appraisal_");

        std::wstring fileName = filePrefix + newName.c_str();
        co_await MainWindow::SaveTextToFileAsync(fileName, fileContent);
        co_await MainWindow::SaveAppraisalToJsonAsync(newName, newSummary, newTagsRaw, newNotes);
    }

    void MainWindow::HistoryDialog_CancelClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        HistoryDialog().Hide();
    }

    // Deletes the appraisal from memory and the JSON file
    winrt::fire_and_forget MainWindow::HistoryDialog_DeleteClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {

        // Hide the dialog immediately for better UX
        HistoryDialog().Hide();

        // Convert the stored original name back to an hstring to use as our search key
        winrt::hstring targetName = winrt::to_hstring(m_originalAppraisalName);

        // 1. Remove from the internal memory list (m_allAppraisals)
        auto it = std::remove_if(m_allAppraisals.begin(), m_allAppraisals.end(),
            [&](const AppraisalItem& app) { return app.Name == targetName; });

        if (it != m_allAppraisals.end()) {
            m_allAppraisals.erase(it, m_allAppraisals.end());
        }

        // 2. Remove from the JSON file on disk
		using namespace winrt::Windows::Storage;
		using namespace winrt::Windows::Data::Json;

		StorageFolder localFolder = ApplicationData::Current().LocalFolder();

		// Safely check if the file exists before trying to read it
		IStorageItem item = co_await localFolder.TryGetItemAsync(L"appraisals.json");

		if (item) {
			StorageFile file = item.as<StorageFile>();
			winrt::hstring jsonString = co_await FileIO::ReadTextAsync(file);
			JsonObject rootObject;

			// Parse the existing JSON
			if (JsonObject::TryParse(jsonString, rootObject)) {

				// If the key exists, remove it and save the file
				if (rootObject.HasKey(targetName)) {
					rootObject.Remove(targetName);

					// Overwrite the file with the new, smaller JSON string
					co_await FileIO::WriteTextAsync(file, rootObject.Stringify());
				}
			}
		}

        // 3. Re-render the UI grid so the deleted item disappears
        MainWindow::RenderAppraisalsList();
    }
}