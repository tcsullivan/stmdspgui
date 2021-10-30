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

/**
 * TODO list:
 * - Improve signal generator audio streaming.
 */

#include "stmdsp.hpp"

#include "imgui.h"
#include "ImGuiFileDialog.h"
#include "wav.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

extern std::string tempFileName;
extern void log(const std::string& str);

extern std::vector<stmdsp::dacsample_t> deviceGenLoadFormulaEval(const std::string_view);

std::shared_ptr<stmdsp::device> m_device;

static const std::array<const char *, 6> sampleRateList {{
    "8 kHz",
    "16 kHz",
    "20 kHz",
    "32 kHz",
    "48 kHz",
    "96 kHz"
}};
static const char *sampleRatePreview = sampleRateList[0];
static const std::array<unsigned int, 6> sampleRateInts {{
    8'000,
    16'000,
    20'000,
    32'000,
    48'000,
    96'000
}};

static bool measureCodeTime = false;
static bool drawSamples = false;
static bool logResults = false;
static bool genRunning = false;
static bool drawSamplesInput = false;

static bool popupRequestBuffer = false;
static bool popupRequestSiggen = false;
static bool popupRequestDraw = false;
static bool popupRequestLog = false;

static std::timed_mutex mutexDrawSamples;
static std::timed_mutex mutexDeviceLoad;

static std::ofstream logSamplesFile;
static wav::clip wavOutput;

static std::deque<stmdsp::dacsample_t> drawSamplesQueue;
static std::deque<stmdsp::dacsample_t> drawSamplesInputQueue;
static double drawSamplesTimeframe = 1.0; // seconds
static unsigned int drawSamplesBufferSize = 1;

static void measureCodeTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto cycles = device->continuous_start_get_measurement();
    log(std::string("Execution time: ") + std::to_string(cycles) + " cycles.");
}

static void drawSamplesTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;

    const bool doLogger = logResults && logSamplesFile.good();

    const double bufferSize = m_device->get_buffer_size();
    const double sampleRate = sampleRateInts[m_device->get_sample_rate()];
    unsigned long bufferTime = bufferSize / sampleRate * 0.975 * 1e6;

    std::unique_lock<std::timed_mutex> lockDraw (mutexDrawSamples, std::defer_lock);
    std::unique_lock<std::timed_mutex> lockDevice (mutexDeviceLoad, std::defer_lock);

    while (m_device && m_device->is_running()) {
        auto next = std::chrono::high_resolution_clock::now() +
                    std::chrono::microseconds(bufferTime);

        std::vector<stmdsp::dacsample_t> chunk;

	if (lockDevice.try_lock_until(next)) {
            chunk = m_device->continuous_read();
            int tries = -1;
            while (chunk.empty() && m_device->is_running()) {
                if (++tries == 100)
                    break;
                std::this_thread::sleep_for(std::chrono::microseconds(20));
                chunk = m_device->continuous_read();
            }
	    lockDevice.unlock();
	} else {
            // Cooldown.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

        if (drawSamplesInput && popupRequestDraw) {
            std::vector<stmdsp::dacsample_t> chunk2;

	    if (lockDevice.try_lock_for(std::chrono::milliseconds(1))) {
                chunk2 = m_device->continuous_read_input();
                int tries = -1;
                while (chunk2.empty() && m_device->is_running()) {
                    if (++tries == 100)
                        break;
                    std::this_thread::sleep_for(std::chrono::microseconds(20));
                    chunk2 = m_device->continuous_read_input();
                }
	        lockDevice.unlock();
            }

	    lockDraw.lock();
            auto i = chunk2.cbegin();
            for (const auto& s : chunk) {
                drawSamplesQueue.push_back(s);
                drawSamplesInputQueue.push_back(*i++);
            }
	    lockDraw.unlock();
        } else if (!doLogger) {
	    lockDraw.lock();
            for (const auto& s : chunk)
                drawSamplesQueue.push_back(s);
	    lockDraw.unlock();
        } else {
	    lockDraw.lock();
            for (const auto& s : chunk) {
                drawSamplesQueue.push_back(s);
                logSamplesFile << s << '\n';
            }
	    lockDraw.unlock();
        }
        
        std::this_thread::sleep_until(next);
    }
}

static void feedSigGenTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;

    const auto bufferSize = m_device->get_buffer_size();
    const double sampleRate = sampleRateInts[m_device->get_sample_rate()];
    const unsigned long delay = bufferSize / sampleRate * 0.975 * 1e6;

    std::vector<stmdsp::adcsample_t> wavBuf (bufferSize, 2048);

    std::unique_lock<std::timed_mutex> lockDevice (mutexDeviceLoad, std::defer_lock);

    lockDevice.lock();
    // One (or both) of these freezes the device...
    m_device->siggen_upload(wavBuf.data(), wavBuf.size());
    //m_device->siggen_start();
    lockDevice.unlock();

    std::this_thread::sleep_for(std::chrono::microseconds(delay));

    return;
    while (genRunning) {
        auto next = std::chrono::high_resolution_clock::now() +
                    std::chrono::microseconds(delay);

        auto src = reinterpret_cast<uint16_t *>(wavOutput.next(bufferSize));
        for (auto& w : wavBuf)
            w = *src++ / 16 + 2048;

        if (lockDevice.try_lock_until(next)) {
            m_device->siggen_upload(wavBuf.data(), wavBuf.size());
	    lockDevice.unlock();
            std::this_thread::sleep_until(next);
        }
    }
}

static void statusTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;

    while (device->connected()) {
        std::unique_lock<std::timed_mutex> lockDevice (mutexDeviceLoad, std::defer_lock);
        lockDevice.lock();
        auto [status, error] = device->get_status();
        lockDevice.unlock();

        if (error != stmdsp::Error::None) {
            if (error == stmdsp::Error::NotIdle) {
                log("Error: Device already running...");
            } else if (error == stmdsp::Error::ConversionAborted) {
                log("Error: Algorithm unloaded, a fault occurred!");
            } else {
                log("Error: Device had an issue...");
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
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
            if (m_device) {
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

    if (ImGuiFileDialog::Instance()->Display("ChooseFileLogGen",
                                             ImGuiWindowFlags_NoCollapse,
                                             ImVec2(460, 540)))
    {
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
        }

        ImGuiFileDialog::Instance()->Close();
    }
}

void deviceRenderDraw()
{
    if (popupRequestDraw) {
        static std::vector<stmdsp::dacsample_t> buffer;
        static decltype(buffer.begin()) bufferCursor;
        static std::vector<stmdsp::dacsample_t> bufferInput;
        static decltype(bufferInput.begin()) bufferInputCursor;
        static unsigned int yMinMax = 4095;

        ImGui::Begin("draw", &popupRequestDraw);
        ImGui::Text("Draw input ");
        ImGui::SameLine();
        ImGui::Checkbox("", &drawSamplesInput);
        ImGui::SameLine();
        ImGui::Text("Time: %0.3f sec", drawSamplesTimeframe);
        ImGui::SameLine();
        if (ImGui::Button("-", {30, 0})) {
            drawSamplesTimeframe = std::max(drawSamplesTimeframe / 2., 0.0078125);
            auto sr = sampleRateInts[m_device->get_sample_rate()];
            auto tf = drawSamplesTimeframe;
            drawSamplesBufferSize = std::round(sr * tf);
        }
        ImGui::SameLine();
        if (ImGui::Button("+", {30, 0})) {
            drawSamplesTimeframe = std::min(drawSamplesTimeframe * 2, 32.);
            auto sr = sampleRateInts[m_device->get_sample_rate()];
            auto tf = drawSamplesTimeframe;
            drawSamplesBufferSize = std::round(sr * tf);
        }
        ImGui::SameLine();
        ImGui::Text("Y-minmax: %u", yMinMax);
        ImGui::SameLine();
        if (ImGui::Button("--", {30, 0})) {
            yMinMax = std::max(63u, yMinMax >> 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("++", {30, 0})) {
            yMinMax = std::min(4095u, (yMinMax << 1) | 1);
        }

        static unsigned long csize = 0;
        if (buffer.size() != drawSamplesBufferSize) {
            buffer.resize(drawSamplesBufferSize);
            bufferInput.resize(drawSamplesBufferSize);
            bufferCursor = buffer.begin();
            bufferInputCursor = bufferInput.begin();
            csize = drawSamplesBufferSize / (60. * drawSamplesTimeframe) * 1.025;
        }

        {
            std::scoped_lock lock (mutexDrawSamples);
            auto count = std::min(drawSamplesQueue.size(), csize);
            for (auto i = count; i; --i) {
                *bufferCursor = drawSamplesQueue.front();
                drawSamplesQueue.pop_front();
                if (++bufferCursor == buffer.end())
                    bufferCursor = buffer.begin();
            }
            
            if (drawSamplesInput) {
                auto count = std::min(drawSamplesInputQueue.size(), csize);
                for (auto i = count; i; --i) {
                    *bufferInputCursor = drawSamplesInputQueue.front();
                    drawSamplesInputQueue.pop_front();
                    if (++bufferInputCursor == bufferInput.end())
                        bufferInputCursor = bufferInput.begin();
                }
            }
        }

        auto drawList = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        auto size = ImGui::GetWindowSize();
        p0.y += 65;
        size.y -= 70;
        drawList->AddRectFilled(p0, {p0.x + size.x, p0.y + size.y}, IM_COL32(0, 0, 0, 255));

        const float di = static_cast<float>(buffer.size()) / size.x;
        const float dx = std::ceil(size.x / static_cast<float>(buffer.size()));
        ImVec2 pp = p0;
        float i = 0;
        while (pp.x < p0.x + size.x) {
            unsigned int idx = i;
            float n = std::clamp((buffer[idx] - 2048.) / yMinMax, -0.5, 0.5);
            i += di;

            ImVec2 next (pp.x + dx, p0.y + size.y * (0.5 - n));
            drawList->AddLine(pp, next, ImGui::GetColorU32(IM_COL32(255, 0, 0, 255)));
            pp = next;
        }

        if (drawSamplesInput) {
            ImVec2 pp = p0;
            float i = 0;
            while (pp.x < p0.x + size.x) {
                unsigned int idx = i;
                float n = std::clamp((bufferInput[idx] - 2048.) / yMinMax, -0.5, 0.5);
                i += di;

                ImVec2 next (pp.x + dx, p0.y + size.y * (0.5 - n));
                drawList->AddLine(pp, next, ImGui::GetColorU32(IM_COL32(0, 0, 255, 255)));
                pp = next;
            }
        }

        ImGui::End();
    }
}

void deviceRenderMenu()
{
    if (ImGui::BeginMenu("Run")) {
        bool isConnected = m_device ? true : false;
        bool isRunning = isConnected && m_device->is_running();

        static const char *connectLabel = "Connect";
        if (ImGui::MenuItem(connectLabel, nullptr, false, !isConnected || (isConnected && !isRunning))) {
            deviceConnect();
            isConnected = m_device ? true : false;
            connectLabel = isConnected ? "Disconnect" : "Connect";
        }

        ImGui::Separator();
        static const char *startLabel = "Start";
        if (ImGui::MenuItem(startLabel, nullptr, false, isConnected)) {
            startLabel = isRunning ? "Start" : "Stop";
            deviceStart();
        }

        if (ImGui::MenuItem("Upload algorithm", nullptr, false, isConnected && !isRunning))
            deviceAlgorithmUpload();
        if (ImGui::MenuItem("Unload algorithm", nullptr, false, isConnected && !isRunning))
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
        if (ImGui::MenuItem("Set buffer size...", nullptr, false, isConnected && !isRunning)) {
            popupRequestBuffer = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load signal generator", nullptr, false, isConnected && !isRunning)) {
            popupRequestSiggen = true;
        }
        static const char *startSiggenLabel = "Start signal generator";
        if (ImGui::MenuItem(startSiggenLabel, nullptr, false, isConnected)) {
            if (m_device) {
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
        for (unsigned int i = 0; i < sampleRateList.size(); ++i) {
            if (ImGui::Selectable(sampleRateList[i])) {
                sampleRatePreview = sampleRateList[i];
                if (m_device && !m_device->is_running()) {
                    do {
                        m_device->set_sample_rate(i);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    } while (m_device->get_sample_rate() != i);

                    drawSamplesBufferSize = std::round(sampleRateInts[i] * drawSamplesTimeframe);
                }
            }
        }
        ImGui::EndCombo();
    }
}

void deviceConnect()
{
    static std::thread statusThread;

    if (!m_device) {
        stmdsp::scanner scanner;
        if (auto devices = scanner.scan(); devices.size() > 0) {
            try {
                m_device.reset(new stmdsp::device(devices.front()));
            } catch (...) {
                log("Failed to connect (check permissions?).");
		m_device.reset();
            }

            if (m_device) {
                if (m_device->connected()) {
                    auto sri = m_device->get_sample_rate();
                    sampleRatePreview = sampleRateList[sri];
                    drawSamplesBufferSize = std::round(sampleRateInts[sri] * drawSamplesTimeframe);
                    log("Connected!");
                    statusThread = std::thread(statusTask, m_device);
                    statusThread.detach();
                } else {
                    m_device.reset();
                    log("Failed to connect.");
                }
            }
        } else {
            log("No devices found.");
        }
    } else {
        m_device->disconnect();
        if (statusThread.joinable())
            statusThread.join();
        m_device.reset();
        log("Disconnected.");
    }
}

void deviceStart()
{
    if (!m_device) {
        log("No device connected.");
        return;
    }

    if (m_device->is_running()) {
        {
            std::scoped_lock lock (mutexDrawSamples);
            std::scoped_lock lock2 (mutexDeviceLoad);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
            m_device->continuous_stop();
        }
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
    if (!m_device) {
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
    if (!m_device) {
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

        if (m_device)
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
        if (m_device)
            m_device->siggen_upload(&samples[0], samples.size());

        log("Generator ready.");
    } else {
        log("Error: Bad formula.");
    }
}

