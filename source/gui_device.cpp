#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiFileDialog.h"

#include "stmdsp.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>

// Used for status queries and buffer size configuration.
extern std::shared_ptr<stmdsp::device> m_device;

void deviceAlgorithmUnload();
void deviceAlgorithmUpload();
bool deviceConnect();
void deviceGenLoadFormula(std::string_view list);
void deviceGenLoadList(std::string_view list);
bool deviceGenStartToggle();
void deviceLoadAudioFile(const std::string& file);
void deviceLoadLogFile(const std::string& file);
void deviceSetSampleRate(unsigned int index);
void deviceSetInputDrawing(bool enabled);
void deviceStart(bool measureCodeTime, bool logResults, bool drawSamples);
void deviceUpdateDrawBufferSize(double timeframe);
void pullFromDrawQueue(
    std::vector<stmdsp::dacsample_t>& buffer,
    decltype(buffer.begin())& bufferCursor,
    double timeframe);
void pullFromInputDrawQueue(
    std::vector<stmdsp::dacsample_t>& buffer,
    decltype(buffer.begin())& bufferCursor,
    double timeframe);

static std::string sampleRatePreview = "?";
static bool measureCodeTime = false;
static bool logResults = false;
static bool drawSamples = false;
static bool popupRequestBuffer = false;
static bool popupRequestSiggen = false;
static bool popupRequestLog = false;
static double drawSamplesTimeframe = 1.0; // seconds

static std::string getSampleRatePreview(unsigned int rate)
{
    return std::to_string(rate / 1000) + " kHz";
}

void deviceRenderMenu()
{
    auto addMenuItem = [](const std::string& label, bool enable, auto action) {
        if (ImGui::MenuItem(label.c_str(), nullptr, false, enable)) {
            action();
        }
    };

    if (ImGui::BeginMenu("Run")) {
        const bool isConnected = m_device ? true : false;
        const bool isRunning = isConnected && m_device->is_running();

        static std::string connectLabel ("Connect");
        addMenuItem(connectLabel, !isConnected || !isRunning, [&] {
                if (deviceConnect()) {
                    connectLabel = "Disconnect";
                    sampleRatePreview =
                        getSampleRatePreview(m_device->get_sample_rate());
                    deviceUpdateDrawBufferSize(drawSamplesTimeframe);
                } else {
                    connectLabel = "Connect";
                }
            });

        ImGui::Separator();

        static std::string startLabel ("Start");
        addMenuItem(startLabel, isConnected, [&] {
                startLabel = isRunning ? "Start" : "Stop";
                deviceStart(measureCodeTime, logResults, drawSamples);
                if (logResults && isRunning)
                    logResults = false;
            });
        addMenuItem("Upload algorithm", isConnected && !isRunning,
            deviceAlgorithmUpload);
        addMenuItem("Unload algorithm", isConnected && !isRunning,
            deviceAlgorithmUnload);

        ImGui::Separator();
        if (!isConnected || isRunning)
            ImGui::PushDisabled();

        ImGui::Checkbox("Measure Code Time", &measureCodeTime);
        ImGui::Checkbox("Draw samples", &drawSamples);
        if (ImGui::Checkbox("Log results...", &logResults)) {
            if (logResults)
                popupRequestLog = true;
        }

        if (!isConnected || isRunning)
            ImGui::PopDisabled();

        addMenuItem("Set buffer size...", isConnected && !isRunning,
            [] { popupRequestBuffer = true; });

        ImGui::Separator();

        addMenuItem("Load signal generator",
            isConnected && !m_device->is_siggening(),
            [] { popupRequestSiggen = true; });

        static std::string startSiggenLabel ("Start signal generator");
        addMenuItem(startSiggenLabel, isConnected, [&] {
                const bool running = deviceGenStartToggle();
                startSiggenLabel = running ? "Stop signal generator"
                                           : "Start signal generator";
            });

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

    const bool enable =
        m_device && !m_device->is_running() && !m_device->is_siggening();
    if (!enable)
        ImGui::PushDisabled();

    if (ImGui::BeginCombo("", sampleRatePreview.c_str())) {
        extern std::array<unsigned int, 6> sampleRateInts;

        for (const auto& r : sampleRateInts) {
            const auto s = getSampleRatePreview(r);
            if (ImGui::Selectable(s.c_str())) {
                sampleRatePreview = s;
                deviceSetSampleRate(r);
                deviceUpdateDrawBufferSize(drawSamplesTimeframe);
            }
        }

        ImGui::EndCombo();
    }

    if (!enable)
        ImGui::PopDisabled();
}

void deviceRenderWidgets()
{
    static std::string siggenInput;
    static int siggenOption = 0;

    if (popupRequestSiggen) {
        popupRequestSiggen = false;
        siggenInput.clear();
        ImGui::OpenPopup("siggen");
    } else if (popupRequestBuffer) {
        popupRequestBuffer = false;
        ImGui::OpenPopup("buffer");
    } else if (popupRequestLog) {
        popupRequestLog = false;
        ImGuiFileDialog::Instance()->OpenModal(
            "ChooseFileLogGen", "Choose File", ".csv", ".");
    }

    if (ImGui::BeginPopup("siggen")) {
        if (ImGui::RadioButton("List", &siggenOption, 0))
            siggenInput.clear();
        ImGui::SameLine();
        if (ImGui::RadioButton("Formula", &siggenOption, 1))
            siggenInput.clear();
        ImGui::SameLine();
        if (ImGui::RadioButton("Audio File", &siggenOption, 2))
            siggenInput.clear();

        if (siggenOption == 2) {
            if (ImGui::Button("Choose File")) {
                // This dialog will override the siggen popup, closing it.
                ImGuiFileDialog::Instance()->OpenModal(
                    "ChooseFileLogGen", "Choose File", ".wav", ".");
            }
        } else {
            ImGui::Text(siggenOption == 0 ? "Enter a list of numbers:"
                                          : "Enter a formula. f(x) = ");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {.8, .8, .8, 1});
            ImGui::InputText("", siggenInput.data(), siggenInput.size());
            ImGui::PopStyleColor();
        }

        if (ImGui::Button("Cancel")) {
            siggenInput.clear();
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Save")) {
            switch (siggenOption) {
            case 0:
                deviceGenLoadList(siggenInput);
                break;
            case 1:
                deviceGenLoadFormula(siggenInput);
                break;
            case 2:
                break;
            }

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("buffer")) {
        static std::string bufferSizeInput ("4096");
        ImGui::Text("Please enter a new sample buffer size (100-4096):");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, {.8, .8, .8, 1});
        ImGui::InputText("",
            bufferSizeInput.data(),
            bufferSizeInput.size(),
            ImGuiInputTextFlags_CharsDecimal);
        ImGui::PopStyleColor();
        if (ImGui::Button("Save")) {
            if (m_device) {
                int n = std::clamp(std::stoi(bufferSizeInput), 100, 4096);
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

            if (ext.compare(".wav") == 0)
                deviceLoadAudioFile(filePathName);
            else if (ext.compare(".csv") == 0)
                deviceLoadLogFile(filePathName);
        }

        ImGuiFileDialog::Instance()->Close();
    }
}

void deviceRenderDraw()
{
    if (drawSamples) {
        static std::vector<stmdsp::dacsample_t> buffer;
        static decltype(buffer.begin()) bufferCursor;
        static std::vector<stmdsp::dacsample_t> bufferInput;
        static decltype(bufferInput.begin()) bufferInputCursor;

        static bool drawSamplesInput = false;
        static unsigned int yMinMax = 4095;

        ImGui::Begin("draw", &drawSamples);
        ImGui::Text("Draw input ");
        ImGui::SameLine();
        if (ImGui::Checkbox("", &drawSamplesInput))
            deviceSetInputDrawing(drawSamplesInput);
        ImGui::SameLine();
        ImGui::Text("Time: %0.3f sec", drawSamplesTimeframe);
        ImGui::SameLine();
        if (ImGui::Button("-", {30, 0})) {
            drawSamplesTimeframe = std::max(drawSamplesTimeframe / 2., 0.0078125);
            deviceUpdateDrawBufferSize(drawSamplesTimeframe);
        }
        ImGui::SameLine();
        if (ImGui::Button("+", {30, 0})) {
            drawSamplesTimeframe = std::min(drawSamplesTimeframe * 2, 32.);
            deviceUpdateDrawBufferSize(drawSamplesTimeframe);
        }
        ImGui::SameLine();
        ImGui::Text("Y: +/-%1.2fV", 3.3f * (static_cast<float>(yMinMax) / 4095.f));
        ImGui::SameLine();
        if (ImGui::Button(" - ", {30, 0})) {
            yMinMax = std::max(63u, yMinMax >> 1);
        }
        ImGui::SameLine();
        if (ImGui::Button(" + ", {30, 0})) {
            yMinMax = std::min(4095u, (yMinMax << 1) | 1);
        }

        pullFromDrawQueue(buffer, bufferCursor, drawSamplesTimeframe);
        if (drawSamplesInput)
            pullFromInputDrawQueue(bufferInput, bufferInputCursor, drawSamplesTimeframe);

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
