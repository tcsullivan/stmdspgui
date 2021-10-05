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

#include <string>

extern ImFont *fontSans;
extern ImFont *fontMono;

extern bool guiInitialize();
extern void guiHandleEvents(bool& done);
extern void guiShutdown();
extern void guiRender(void (*func)());

extern void fileRenderMenu();
extern void fileRenderDialog();
extern void fileScanTemplates();

extern void codeEditorInit();
extern void codeRenderMenu();
extern void codeRenderToolbar();
extern void codeRenderWidgets();

extern void deviceRenderDraw();
extern void deviceRenderMenu();
extern void deviceRenderToolbar();
extern void deviceRenderWidgets();

// Globals that live here
bool done = false;
stmdsp::device *m_device = nullptr;

static LogView logView;

void log(const std::string& str)
{
    logView.AddLog(str);
}

int main(int, char **)
{
    if (!guiInitialize())
        return -1;

    fileScanTemplates();
    codeEditorInit();

    while (!done) {
        guiHandleEvents(done);

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
        ImGui::SetNextWindowPos({0, 22});
        ImGui::SetNextWindowSize({WINDOW_WIDTH, WINDOW_HEIGHT - 22 - 200});
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Render main controls (order is important).
        ImGui::PushFont(fontSans);
        codeRenderToolbar();
        deviceRenderToolbar();
        fileRenderDialog();
        deviceRenderWidgets();
        ImGui::PopFont();

        ImGui::PushFont(fontMono);
        codeRenderWidgets();
        ImGui::SetNextWindowPos({0, WINDOW_HEIGHT - 200});
        ImGui::SetNextWindowSize({WINDOW_WIDTH, 200});
        logView.Draw("log", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopFont();

        // Finish main view rendering.
        ImGui::End();

        deviceRenderDraw();

        // Draw everything to the screen.
        ImGui::Render();
        guiRender([] {
            ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        });
    }

    guiShutdown();
    return 0;
}

