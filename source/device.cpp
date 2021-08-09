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

extern stmdsp::device *m_device;
extern void log(const std::string& str);

static const char *sampleRateList[6] = {
    "8 kHz",
    "16 kHz",
    "20 kHz",
    "32 kHz",
    "48 kHz",
    "96 kHz"
};
static const char *sampleRatePreview = sampleRateList[0];

static void deviceConnect();
static void deviceStart();

void deviceRenderMenu()
{
    if (ImGui::BeginMenu("Run")) {
        static const char *connectLabel = "Connect";
        if (ImGui::MenuItem(connectLabel)) {
            deviceConnect();
            connectLabel = m_device == nullptr ? "Connect" : "Disconnect";
        }

        static const char *startLabel = "Start";
        if (ImGui::MenuItem(startLabel)) {
            deviceStart();
            startLabel = m_device != nullptr && m_device->is_running() ? "Stop" : "Start";
        }

/**
TODO: Run menu:
measure
draw
log
upload
unload
buffer size
load siggen
run siggen 
 */
        ImGui::MenuItem("Upload algorithm");
        ImGui::MenuItem("Unload algorithm");
        ImGui::MenuItem("Measure Code Time");
        ImGui::MenuItem("Draw samples");
        ImGui::MenuItem("Log results...");
        ImGui::MenuItem("Load signal generator");
        ImGui::MenuItem("Start signal generator");

        ImGui::EndMenu();
    }
}

void deviceRenderToolbar()
{
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
        log("Ready.");
    } else {
        m_device->continuous_start();
        log("Running.");
    }
}

