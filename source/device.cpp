/**
 * @file device.cpp
 * @brief Contains code for device-related UI elements and logic.
 *
 * Copyright (C) 2021 Clyne Sullivan
 *
 * Distributed under the GNU GPL v3 or later. You should have received a copy of
 * the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "stmdsp.hpp"

#include "imgui.h"
#include "ImGuiFileDialog.h"
#include "wav.hpp"

#include <charconv>
#include <fstream>
#include <mutex>
#include <thread>

extern std::string tempFileName;
extern stmdsp::device *m_device;
extern void log(const std::string& str);

extern std::vector<stmdsp::dacsample_t> deviceGenLoadFormulaEval(const std::string_view);

static const char *sampleRateList[6] = {
    "8 kHz",
    "16 kHz",
    "20 kHz",
    "32 kHz",
    "48 kHz",
    "96 kHz"
};
static const char *sampleRatePreview = sampleRateList[0];
static const unsigned int sampleRateInts[6] = {
    8'000,
    16'000,
    20'000,
    32'000,
    48'000,
    96'000
};

static bool measureCodeTime = false;
static bool drawSamples = false;
static bool logResults = false;
static bool genRunning = false;
static bool drawSamplesInput = false;

static bool popupRequestBuffer = false;
static bool popupRequestSiggen = false;
static bool popupRequestDraw = false;
static bool popupRequestLog = false;

static std::mutex mutexDrawSamples;
static std::vector<stmdsp::dacsample_t> drawSamplesBuf;
static std::vector<stmdsp::dacsample_t> drawSamplesBuf2;
static std::ofstream logSamplesFile;
static wav::clip wavOutput;

static void measureCodeTask(stmdsp::device *device)
{
    if (device == nullptr)
        return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto cycles = device->continuous_start_get_measurement();
    log(std::string("Execution time: ") + std::to_string(cycles) + " cycles.");
}

static void drawSamplesTask(stmdsp::device *device)
{
    if (device == nullptr)
        return;

    const bool doLogger = logResults && logSamplesFile.good();

    const auto bsize = m_device->get_buffer_size();
    const float srate = sampleRateInts[m_device->get_sample_rate()];
    const unsigned int delay = bsize / srate * 1000.f * 0.5f;

    while (m_device->is_running()) {
        {
            std::scoped_lock lock (mutexDrawSamples);
            drawSamplesBuf = m_device->continuous_read();
            if (drawSamplesInput && popupRequestDraw)
                drawSamplesBuf2 = m_device->continuous_read_input();
        }

        if (doLogger) {
            for (const auto& s : drawSamplesBuf)
                logSamplesFile << s << '\n';
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    std::fill(drawSamplesBuf.begin(), drawSamplesBuf.end(), 2048);
    std::fill(drawSamplesBuf2.begin(), drawSamplesBuf2.end(), 2048);
}

static void feedSigGenTask(stmdsp::device *device)
{
    if (device == nullptr)
        return;

    const auto bsize = m_device->get_buffer_size();
    const float srate = sampleRateInts[m_device->get_sample_rate()];
    const unsigned int delay = bsize / srate * 1000.f * 0.4f;

    auto wavBuf = new stmdsp::adcsample_t[bsize];

    {
        auto dst = wavBuf;
        auto src = reinterpret_cast<uint16_t *>(wavOutput.next(bsize));
        for (auto i = 0u; i < bsize; ++i)
            *dst++ = *src++ / 16 + 2048;
        m_device->siggen_upload(wavBuf, bsize);
    }

    m_device->siggen_start();

    while (genRunning) {
        auto dst = wavBuf;
        auto src = reinterpret_cast<uint16_t *>(wavOutput.next(bsize));
        for (auto i = 0u; i < bsize; ++i)
            *dst++ = *src++ / 16 + 2048;
        m_device->siggen_upload(wavBuf, bsize);

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    delete[] wavBuf;
}

static void deviceConnect();
static void deviceStart();
static void deviceAlgorithmUpload();
static void deviceAlgorithmUnload();
static void deviceGenLoadList(std::string_view list);
static void deviceGenLoadFormula(std::string_view list);

void deviceRenderWidgets()
{
    static char *siggenBuffer = nullptr;
    static int siggenOption = 0;

    if (popupRequestSiggen) {
        siggenBuffer = new char[65536];
        *siggenBuffer = '\0';
        ImGui::OpenPopup("siggen");
        popupRequestSiggen = false;
    } else if (popupRequestBuffer) {
        ImGui::OpenPopup("buffer");
        popupRequestBuffer = false;
    } else if (popupRequestLog) {
        ImGuiFileDialog::Instance()->OpenModal(
            "ChooseFileLogGen", "Choose File", ".csv", ".");
        popupRequestLog = false;
    }

    if (ImGui::BeginPopup("siggen")) {
        if (ImGui::RadioButton("List", &siggenOption, 0))
            siggenBuffer[0] = '\0';
        ImGui::SameLine();
        if (ImGui::RadioButton("Formula", &siggenOption, 1))
            siggenBuffer[0] = '\0';
        ImGui::SameLine();
        if (ImGui::RadioButton("Audio File", &siggenOption, 2))
            siggenBuffer[0] = '\0';

        switch (siggenOption) {
        case 0:
            ImGui::Text("Enter a list of numbers:");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {.8, .8, .8, 1});
            ImGui::InputText("", siggenBuffer, 65536);
            ImGui::PopStyleColor();
            break;
        case 1:
            ImGui::Text("Enter a formula. f(x) = ");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {.8, .8, .8, 1});
            ImGui::InputText("", siggenBuffer, 65536);
            ImGui::PopStyleColor();
            break;
        case 2:
            if (ImGui::Button("Choose File")) {
                // This dialog will override the siggen popup, closing it.
                ImGuiFileDialog::Instance()->OpenModal(
                    "ChooseFileLogGen", "Choose File", ".wav", ".");
            }
            break;
        }

        if (ImGui::Button("Cancel")) {
            delete[] siggenBuffer;
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Save")) {
            switch (siggenOption) {
            case 0:
                deviceGenLoadList(siggenBuffer);
                break;
            case 1:
                deviceGenLoadFormula(siggenBuffer);
                break;
            case 2:
                break;
            }

            delete[] siggenBuffer;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("buffer")) {
        static char bufferSizeStr[5] = "4096";
        ImGui::Text("Please enter a new sample buffer size (100-4096):");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, {.8, .8, .8, 1});
        ImGui::InputText("", bufferSizeStr, sizeof(bufferSizeStr), ImGuiInputTextFlags_CharsDecimal);
        ImGui::PopStyleColor();
        if (ImGui::Button("Save")) {
            if (m_device != nullptr) {
                int n = std::clamp(std::stoi(bufferSizeStr), 100, 4096);
                m_device->continuous_set_buffer_size(n);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseFileLogGen")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            auto filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            auto ext = filePathName.substr(filePathName.size() - 4);

            if (ext.compare(".wav") == 0) {
                wavOutput = wav::clip(filePathName.c_str());
                if (wavOutput.valid())
                    log("Audio file loaded.");
                else
                    log("Error: Bad WAV audio file.");

                delete[] siggenBuffer;
            } else if (ext.compare(".csv") == 0) {
                logSamplesFile = std::ofstream(filePathName);
                if (logSamplesFile.good())
                    log("Log file ready.");
            }

            ImGuiFileDialog::Instance()->Close();
        }
    }
}

void deviceRenderDraw()
{
    if (popupRequestDraw) {
        ImGui::Begin("draw", &popupRequestDraw);
            ImGui::Checkbox("Draw input", &drawSamplesInput);
            {
                std::scoped_lock lock (mutexDrawSamples);
                auto drawList = ImGui::GetWindowDrawList();
                const ImVec2 p0 = ImGui::GetWindowPos();
                const auto size = ImGui::GetWindowSize();
                //ImVec2 p1 (p0.x + size.x, p0.y + size.y);
                //ImU32 col_a = ImGui::GetColorU32(IM_COL32(0, 0, 0, 255));
                //ImU32 col_b = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255));
                //drawList->AddRectFilledMultiColor(p0, p1, col_a, col_b, col_b, col_a);

                const unsigned int didx = 1.f / (size.x / static_cast<float>(drawSamplesBuf.size()));
                ImVec2 pp = p0;
                for (auto i = 0u; i < drawSamplesBuf.size(); i += didx) {
                    ImVec2 next (pp.x + 1, p0.y + (float)drawSamplesBuf[i] / 4095.f * size.y);
                    drawList->AddLine(pp, next, ImGui::GetColorU32(IM_COL32(128, 0, 0, 255)));
                    pp = next;
                }

                if (drawSamplesInput) {
                    pp = p0;
                    for (auto i = 0u; i < drawSamplesBuf2.size(); i += didx) {
                        ImVec2 next (pp.x + 1, p0.y + (float)drawSamplesBuf2[i] / 4095.f * size.y);
                        drawList->AddLine(pp, next, ImGui::GetColorU32(IM_COL32(0, 0, 128, 255)));
                        pp = next;
                    }
                }
            }
        ImGui::End();
    }
}

void deviceRenderMenu()
{
    if (ImGui::BeginMenu("Run")) {
        bool isConnected = m_device != nullptr;
        bool isRunning = isConnected && m_device->is_running();

        static const char *connectLabel = "Connect";
        if (ImGui::MenuItem(connectLabel)) {
            deviceConnect();
            connectLabel = isConnected ? "Disconnect" : "Connect";
        }

        ImGui::Separator();
        static const char *startLabel = "Start";
        if (ImGui::MenuItem(startLabel, nullptr, false, isConnected)) {
            deviceStart();
            startLabel = isRunning ? "Stop" : "Start";
        }

/**
TODO test siggen formula
TODO improve siggen audio streaming
TODO draw: smoothly chain captures
 */
        if (ImGui::MenuItem("Upload algorithm", nullptr, false, isConnected))
            deviceAlgorithmUpload();
        if (ImGui::MenuItem("Unload algorithm", nullptr, false, isConnected))
            deviceAlgorithmUnload();
        ImGui::Separator();
        if (ImGui::Checkbox("Measure Code Time", &measureCodeTime)) {
            if (!isConnected)
                measureCodeTime = false;
        }
        if (ImGui::Checkbox("Draw samples", &drawSamples)) {
            if (isConnected) {
                if (drawSamples)
                    popupRequestDraw = true;
            } else {
                drawSamples = false;
            }
        }
        if (ImGui::Checkbox("Log results...", &logResults)) {
            if (isConnected) {
                if (logResults)
                    popupRequestLog = true;
                else if (logSamplesFile.is_open())
                    logSamplesFile.close();
            } else {
                logResults = false;
            }
        }
        if (ImGui::MenuItem("Set buffer size...", nullptr, false, isConnected)) {
            popupRequestBuffer = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load signal generator", nullptr, false, isConnected)) {
            popupRequestSiggen = true;
        }
        static const char *startSiggenLabel = "Start signal generator";
        if (ImGui::MenuItem(startSiggenLabel, nullptr, false, isConnected)) {
            if (m_device != nullptr) {
                if (!genRunning) {
                    genRunning = true;
                    if (wavOutput.valid())
                        std::thread(feedSigGenTask, m_device).detach();
                    else
                        m_device->siggen_start();
                    log("Generator started.");
                    startSiggenLabel = "Stop signal generator";
                } else {
                    genRunning = false;
                    m_device->siggen_stop();
                    log("Generator stopped.");
                    startSiggenLabel = "Start signal generator";
                }
            }
        }

        ImGui::EndMenu();
    }
}

void deviceRenderToolbar()
{
    ImGui::SameLine();
    if (ImGui::Button("Upload"))
        deviceAlgorithmUpload();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::BeginCombo("", sampleRatePreview)) {
        for (int i = 0; i < 6; ++i) {
            if (ImGui::Selectable(sampleRateList[i])) {
                sampleRatePreview = sampleRateList[i];
                if (m_device != nullptr && !m_device->is_running())
                    m_device->set_sample_rate(i);
            }
        }
        ImGui::EndCombo();
    }
}

void deviceConnect()
{
    if (m_device == nullptr) {
        stmdsp::scanner scanner;
        if (auto devices = scanner.scan(); devices.size() > 0) {
            m_device = new stmdsp::device(devices.front());
            if (m_device->connected()) {
                sampleRatePreview = sampleRateList[m_device->get_sample_rate()];
                log("Connected!");
            } else {
                delete m_device;
                m_device = nullptr;
                log("Failed to connect.");
            }
        } else {
            log("No devices found.");
        }
    } else {
        delete m_device;
        m_device = nullptr;
        log("Disconnected.");
    }
}

void deviceStart()
{
    if (m_device == nullptr) {
        log("No device connected.");
        return;
    }

    if (m_device->is_running()) {
        m_device->continuous_stop();
        if (logResults) {
            logSamplesFile.close();
            logResults = false;
            log("Log file saved and closed.");
        }
        log("Ready.");
    } else {
        if (measureCodeTime) {
            m_device->continuous_start_measure();
            std::thread(measureCodeTask, m_device).detach();
        } else {
            m_device->continuous_start();
            if (drawSamples || logResults || wavOutput.valid())
                std::thread(drawSamplesTask, m_device).detach();
        }
        log("Running.");
    }
}

void deviceAlgorithmUpload()
{
    if (m_device == nullptr) {
        log("No device connected.");
        return;
    }

    if (m_device->is_running())
        return;

    if (std::ifstream algo (tempFileName + ".o"); algo.good()) {
        std::ostringstream sstr;
        sstr << algo.rdbuf();
        auto str = sstr.str();

        m_device->upload_filter(reinterpret_cast<unsigned char *>(&str[0]), str.size());
        log("Algorithm uploaded.");
    } else {
        log("Algorithm must be compiled first.");
    }
}

void deviceAlgorithmUnload()
{
    if (m_device == nullptr) {
        log("No device connected.");
        return;
    }

    if (!m_device->is_running()) {
        m_device->unload_filter();
        log("Algorithm unloaded.");
    }
}

void deviceGenLoadList(std::string_view listStr)
{
    std::vector<stmdsp::dacsample_t> samples;

    while (listStr.size() > 0 && samples.size() <= stmdsp::SAMPLES_MAX * 2) {
        auto numberEnd = listStr.find_first_not_of("0123456789");

        unsigned long n;
        auto end = numberEnd != std::string_view::npos ? listStr.begin() + numberEnd : listStr.end();
        auto [ptr, ec] = std::from_chars(listStr.begin(), end, n);
        if (ec != std::errc())
            break;

        samples.push_back(n & 4095);
        if (end == listStr.end())
            break;
        listStr = listStr.substr(numberEnd + 1);
    }

    if (samples.size() <= stmdsp::SAMPLES_MAX * 2) {
        // DAC buffer must be of even size
        if ((samples.size() & 1) == 1)
            samples.push_back(samples.back());

        if (m_device != nullptr)
            m_device->siggen_upload(&samples[0], samples.size());
        log("Generator ready.");
    } else {
        log("Error: Too many samples for signal generator.");
    }
}

void deviceGenLoadFormula(std::string_view formula)
{
    auto samples = deviceGenLoadFormulaEval(formula);

    if (samples.size() > 0) {
        if (m_device != nullptr)
            m_device->siggen_upload(&samples[0], samples.size());

        log("Generator ready.");
    } else {
        log("Error: Bad formula.");
    }
}

