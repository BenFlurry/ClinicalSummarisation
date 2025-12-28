#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// Include your logic header
#include "summarisation.hpp"
#include <thread> // Required for background execution

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        // Run the logic as soon as the window is created
        RunModelAsync();
    }

    void MainWindow::RunModelAsync()
    {
        // 1. Update UI to show we are starting
        StatusText().Text(L"Loading Model (This may take a moment)");
        LoadingSpinner().IsActive(true);

        // 2. Start a background thread (so the window doesn't freeze)
        std::thread([this]()
            {
                // --- BACKGROUND THREAD STARTS ---

                // Call your heavy function
                std::string result = handle_go();

                // Convert std::string to WinRT hstring (required for XAML)
                winrt::hstring displayResult = winrt::to_hstring(result);

                // --- BACKGROUND THREAD ENDS ---

                // 3. Jump back to the UI Thread to update the screen
                this->DispatcherQueue().TryEnqueue([this, displayResult]()
                    {
                        // We are now back on the main UI thread
                        StatusText().Text(L"Generation Complete:");
                        LoadingSpinner().IsActive(false);

                        // Display the result
                        OutputText().Text(displayResult);
                    });

            }).detach(); // Detach the thread so it runs independently
    }
}