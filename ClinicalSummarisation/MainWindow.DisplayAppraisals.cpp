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
        // clear the grid of appraisals
        AppraisalsListContainer().Items().Clear();

        std::wstring nameQuery = NameSearchBox().Text().c_str();
        // filtered items to be displayed
        std::vector<AppraisalItem> filtered;

        // loop through appraisals
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

        // sort by date (newest)
        std::sort(filtered.begin(), filtered.end(), [](const AppraisalItem& a, const AppraisalItem& b) {
            return a.Date > b.Date;
            });

        // generate cards
        for (auto const& app : filtered) {
            // 1. Create a Button Wrapper
            Button itemButton;

            // --- GRID LAYOUT CHANGES ---
            itemButton.Width(340);  // Fixed width makes them form columns
            itemButton.Height(220); // Fixed height makes them uniform tiles
            itemButton.Margin({ 0, 0, 12, 12 }); // Spacing between grid items
            itemButton.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            itemButton.VerticalContentAlignment(VerticalAlignment::Stretch); // Ensure content fills the tile
            // ---------------------------

            // Remove default button styling
            itemButton.Background(Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            itemButton.BorderThickness({ 0,0,0,0 });
            itemButton.Padding({ 0,0,0,0 });

            // Store ID for click handler
            itemButton.Tag(winrt::box_value(app.Name));
            itemButton.Click({ this, &MainWindow::OnHistoryItemClick });

            // 2. Card Container
            StackPanel card;
            card.Padding({ 16, 16, 16, 16 });
            card.Background(Application::Current().Resources().Lookup(winrt::box_value(L"LayerFillColorDefaultBrush")).as<Media::Brush>());
            card.CornerRadius({ 8, 8, 8, 8 });

            // Ensure the card fills the button area
            card.HorizontalAlignment(HorizontalAlignment::Stretch);
            card.VerticalAlignment(VerticalAlignment::Stretch);

            // 3. TITLE
            TextBlock title;
            title.Text(app.Name);
            title.Style(Application::Current().Resources().Lookup(winrt::box_value(L"SubtitleTextBlockStyle")).as<Style>());
            title.TextTrimming(TextTrimming::CharacterEllipsis); // Add ellipsis if name is too long

            // 4. METADATA
            std::wstring_view dateView(app.Date);
            winrt::hstring dateStr = L"Date: ";
            winrt::hstring d = dateView.length() >= 10 ? winrt::hstring(dateView.substr(0, 10)) : app.Date;
            dateStr = dateStr + d;

            TextBlock date;
            date.Text(dateStr);
            date.FontSize(12);
            date.Foreground(Media::SolidColorBrush(winrt::Windows::UI::Colors::Gray()));
            date.Margin({ 0, 2, 0, 8 });

            std::wstring tagsJoined = L"Tags: ";
            for (size_t i = 0; i < app.Tags.size(); ++i) {
                tagsJoined += app.Tags[i];
                if (i < app.Tags.size() - 1) tagsJoined += L", ";
            }
            TextBlock tags;
            tags.Text(tagsJoined);
            tags.TextWrapping(TextWrapping::NoWrap); // Keep tags on one line for cleaner grid
            tags.TextTrimming(TextTrimming::CharacterEllipsis);
            tags.FontSize(12);
            tags.Foreground(Media::SolidColorBrush(winrt::Windows::UI::Colors::Gray()));
            tags.Margin({ 0, 0, 0, 8 });

            // 5. BODY: Summary
            TextBlock summary;
            summary.Text(app.Summary);
            summary.TextWrapping(TextWrapping::Wrap);
            summary.TextTrimming(TextTrimming::CharacterEllipsis); // Cut off text if it overflows the tile
            // No MaxLines needed since the tile height will cut it off naturally, 
            // but setting it helps performance
            summary.MaxLines(5);

            // Add elements to card
            card.Children().Append(title);
            card.Children().Append(date);
            card.Children().Append(tags);
            card.Children().Append(summary);

            // Add card to button
            itemButton.Content(card);

            // CRITICAL CHANGE: Use .Items() instead of .Children() for GridView
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
}