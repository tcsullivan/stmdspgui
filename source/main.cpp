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
#include "stmdsp.hpp"

#include <string>

// Externs
extern ImFont *font;

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

extern void deviceRenderMenu();
extern void deviceRenderToolbar();

// Globals that live here
std::string tempFileName;
std::string statusMessage ("Ready.");
bool done = false;
stmdsp::device *m_device = nullptr;

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
        ImGui::SetNextWindowSize({WINDOW_WIDTH, WINDOW_HEIGHT - 22});
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration);
        ImGui::PushFont(font);

        // Render main controls (order is important).
        codeRenderToolbar();
        deviceRenderToolbar();
        fileRenderDialog();
        codeRenderWidgets();

        // Finish main view rendering.
        ImGui::PopFont();
        ImGui::Text(statusMessage.c_str());
        ImGui::End();

        // Draw everything to the screen.
        ImGui::Render();
        guiRender([] {
            ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        });
    }

    guiShutdown();
    return 0;
}

