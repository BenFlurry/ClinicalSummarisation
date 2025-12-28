#pragma once

#include "MainWindow.g.h"

namespace winrt::ClinicalSummarisation::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
    public:
        MainWindow();
        void RunModelAsync();

    };
}

namespace winrt::ClinicalSummarisation::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
