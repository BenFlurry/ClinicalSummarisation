#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {
    // app state machine
    void MainWindow::SetAppState(AppState state) {
        this->DispatcherQueue().TryEnqueue([this, state]() {
            // default back to nothing
            StatusSpinner().Visibility(Visibility::Collapsed);
            ControlButtons().Visibility(Visibility::Collapsed);
            EnrollmentPanel().Visibility(Visibility::Collapsed);
            enrollVoice_btn().IsEnabled(false);
            PostSummarisationButtons().Visibility(Visibility::Collapsed);
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
