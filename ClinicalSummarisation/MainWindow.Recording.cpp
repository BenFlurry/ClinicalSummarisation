#include "pch.h"
#include "MainWindow.xaml.h"

// for microphone searching
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ClinicalSummarisation::implementation {
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

            if (m_engine->IsCancelled()) { 
                MainWindow::SetAppState(AppState::SummarisationComplete);
                return; 
            }

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

    // when user cancels the recording mid-run
    void MainWindow::cancelRecording_Click(IInspectable const&, RoutedEventArgs const&) {
        // stop transcription processing
        m_engine->Cancel();

        // stop the recorder
        if (m_recorder) {
            m_recorder->Stop();
        }

        // 3. Optional: If your bridge has a Clear() or Flush() method, call it here 
        // to empty any backed-up audio chunks so the next recording starts fresh.
        // m_bridge->Clear();

        // 4. Reset the UI to your default/idle state (replace AppState::Ready with whatever your idle state is named)
    }
}