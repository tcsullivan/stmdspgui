/**
 * @file file.cpp
 * @brief Contains code for file-management-related UI elements and logic.
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
#include "ImGuiFileDialog.h"
#include "TextEditor.h"

#include "stmdsp_code.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

extern TextEditor editor;
extern std::string statusMessage;

enum class FileAction {
    None,
    Open,
    Save,
    SaveAs
};
static FileAction fileAction = FileAction::None;
static std::string fileCurrentPath;
static std::vector<std::filesystem::path> fileTemplateList;

static void saveCurrentFile()
{
    if (std::ofstream ofs (fileCurrentPath, std::ios::binary); ofs.good()) {
        const auto& text = editor.GetText();
        ofs.write(text.data(), text.size());
        statusMessage = "Saved.";
    }
}

static void openCurrentFile()
{
    if (std::ifstream ifs (fileCurrentPath); ifs.good()) {
        std::ostringstream sstr;
        sstr << ifs.rdbuf();
        editor.SetText(sstr.str());
    }
}

void fileScanTemplates()
{
    auto path = std::filesystem::current_path() / "templates";
    for (const auto& file : std::filesystem::recursive_directory_iterator{path})
        fileTemplateList.push_back(file.path());
}

void fileRenderMenu()
{
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New")) {
            // TODO modified?
            fileCurrentPath.clear();
            editor.SetText(stmdsp::file_content);
            statusMessage = "Ready.";
        }

        if (ImGui::MenuItem("Open")) {
            fileAction = FileAction::Open;
            ImGuiFileDialog::Instance()->OpenDialog(
                "ChooseFileDlgKey", "Choose File", ".cpp", ".");
        }

        if (ImGui::BeginMenu("Open Template")) {
            for (const auto& file : fileTemplateList) {
                if (ImGui::MenuItem(file.filename().c_str())) {
                    fileCurrentPath = file.string();
                    openCurrentFile();

                    // Treat like new file.
                    fileCurrentPath.clear();
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Save")) {
            if (fileCurrentPath.size() > 0) {
                saveCurrentFile();
            } else {
                fileAction = FileAction::SaveAs;
                ImGuiFileDialog::Instance()->OpenDialog(
                    "ChooseFileDlgKey", "Choose File", ".cpp", ".");
            }
        }

        if (ImGui::MenuItem("Save As")) {
            fileAction = FileAction::SaveAs;
            ImGuiFileDialog::Instance()->OpenDialog(
                "ChooseFileDlgKey", "Choose File", ".cpp", ".");
        }

        if (ImGui::MenuItem("Quit")) {
            extern bool done;
            done = true;
        }

        ImGui::EndMenu();
    }
}

void fileRenderDialog()
{
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();

            switch (fileAction) {
            case FileAction::None:
                break;
            case FileAction::Open:
                fileCurrentPath = filePathName;
                openCurrentFile();
                statusMessage = "Ready.";
                break;
            case FileAction::SaveAs:
                fileCurrentPath = filePathName;
                saveCurrentFile();
                break;
            }
        }
        
        ImGuiFileDialog::Instance()->Close();
    }
}

