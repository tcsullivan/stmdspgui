/**
 * @file main.cpp
 * @brief Program entry point and main loop.
 *
 * Copyright (C) 2021 Clyne Sullivan
 *
 * Distributed under the GNU GPL v3 or later. You should have received a copy of
 * the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "imgui.h"
#include "backends/imgui_impl_sdl.h"
#include "backends/imgui_impl_opengl2.h"

#include "config.h"
#include "logview.h"
#include "stmdsp.hpp"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

extern ImFont *fontSans;
extern ImFont *fontMono;

bool guiInitialize();
bool guiHandleEvents();
void guiShutdown();
void guiRender(void (*func)());

void fileRenderMenu();
void fileRenderDialog();
void fileInit();

void codeEditorInit();
void codeRenderMenu();
void codeRenderToolbar();
void codeRenderWidgets();

void deviceRenderDraw();
void deviceRenderMenu();
void deviceRenderToolbar();
void deviceRenderWidgets();

static LogView logView;

void log(const std::string& str)
{
    logView.AddLog(str);
}

static void renderWindow();

int main(int, char **)
{
    if (!guiInitialize())
        return -1;

    codeEditorInit();
    fileInit();

    while (1) {
        constexpr std::chrono::duration<double> fpsDelay (1. / 60.);
        const auto endTime = std::chrono::steady_clock::now() + fpsDelay;

        const bool isDone = guiHandleEvents();
        if (!isDone) {
            renderWindow();
            std::this_thread::sleep_until(endTime);
        } else {
            break;
        }
    }

    guiShutdown();
    return 0;
}

void renderWindow()
{
    // Start the new window frame and render the menu bar.
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
        fileRenderMenu();
        deviceRenderMenu();
        codeRenderMenu();
        ImGui::EndMainMenuBar();
    }

    // Begin the main view which the controls will be drawn onto.
    constexpr float LOGVIEW_HEIGHT = 200;
    constexpr ImVec2 WINDOW_POS (0, 22);
    constexpr ImVec2 WINDOW_SIZE (WINDOW_WIDTH, WINDOW_HEIGHT - 22 - LOGVIEW_HEIGHT);
    ImGui::SetNextWindowPos(WINDOW_POS);
    ImGui::SetNextWindowSize(WINDOW_SIZE);
    ImGui::Begin("main", nullptr,
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Render main controls (order is important).
    {
        ImGui::PushFont(fontSans);
        codeRenderToolbar();
        deviceRenderToolbar();
        fileRenderDialog();
        deviceRenderWidgets();
        ImGui::PopFont();

        ImGui::PushFont(fontMono);
        codeRenderWidgets();
        ImGui::SetNextWindowPos({0, WINDOW_HEIGHT - LOGVIEW_HEIGHT});
        ImGui::SetNextWindowSize({WINDOW_WIDTH, LOGVIEW_HEIGHT});
        logView.Draw("log", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopFont();
    }

    ImGui::End();

    deviceRenderDraw();

    // Draw everything to the screen.
    ImGui::Render();
    guiRender([] {
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    });
}

