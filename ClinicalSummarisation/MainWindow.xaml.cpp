#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif


#pragma comment(lib, "shell32.lib")

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {

    MainWindow::MainWindow() {

        InitializeComponent();
        MainWindow::InitialiseApplication();
        MainWindow::loadMicrophones();
        MainWindow::InitialiseDatabase();
        MainWindow::InitialiseUI();
        MainWindow::InitialiseTranscription();
    }
}


