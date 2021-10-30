/**
 * @file code.cpp
 * @brief Contains code for algorithm-code-related UI elements and logic.
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
#include "TextEditor.h"

#include "config.h"
#include "stmdsp.hpp"
#include "stmdsp_code.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern std::shared_ptr<stmdsp::device> m_device;

extern void log(const std::string& str);

TextEditor editor; // file.cpp
std::string tempFileName; // device.cpp
static std::string editorCompiled;

static std::string newTempFileName();
static void compileEditorCode();
static void disassembleCode();

void codeEditorInit()
{
    editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    editor.SetPalette(TextEditor::GetLightPalette());
}

void codeRenderMenu()
{
    if (ImGui::BeginMenu("Code")) {
        if (ImGui::MenuItem("Compile code"))
            compileEditorCode();
        if (ImGui::MenuItem("Show disassembly"))
            disassembleCode();
        ImGui::EndMenu();
    }
}

void codeRenderToolbar()
{
    if (ImGui::Button("Compile"))
        compileEditorCode();
}

void codeRenderWidgets()
{
    editor.Render("code", {WINDOW_WIDTH - 15, 450}, true);
}

void compileEditorCode()
{
    log("Compiling...");

    // Scrap cached build if there are changes
    if (editor.GetText().compare(editorCompiled) != 0) {
        std::filesystem::remove(tempFileName + ".o");
        std::filesystem::remove(tempFileName + ".orig.o");
    }

    stmdsp::platform platform;
    if (m_device) {
        platform = m_device->get_platform();
    } else {
        // Assume a default.
        platform = stmdsp::platform::L4;
    }

    if (tempFileName.size() == 0)
        tempFileName = newTempFileName();

    {
        std::ofstream file (tempFileName, std::ios::trunc | std::ios::binary);

        auto file_text = platform == stmdsp::platform::L4 ? stmdsp::file_header_l4
                                                          : stmdsp::file_header_h7;
        auto samples_text = std::to_string(m_device ? m_device->get_buffer_size()
                                                    : stmdsp::SAMPLES_MAX);
        for (std::size_t i = 0; (i = file_text.find("$0", i)) != std::string::npos;) {
            file_text.replace(i, 2, samples_text);
            i += 2;
        }

        file << file_text;
        file << "\n";
        file << editor.GetText();
    }

    constexpr const char *script_ext =
#ifndef STMDSP_WIN32
        ".sh";
#else
        ".bat";
#endif

    {
        std::ofstream makefile (tempFileName + script_ext, std::ios::binary);
        auto make_text = platform == stmdsp::platform::L4 ? stmdsp::makefile_text_l4
                                                          : stmdsp::makefile_text_h7;
        auto cwd = std::filesystem::current_path().string();
        for (std::size_t i = 0; (i = make_text.find("$0", i)) != std::string::npos;) {
            make_text.replace(i, 2, tempFileName);
            i += 2;
        }
        for (std::size_t i = 0; (i = make_text.find("$1", i)) != std::string::npos;) {
            make_text.replace(i, 2, cwd);
            i += 2;
        }

        makefile << make_text;
    }

    auto makeOutput = tempFileName + script_ext + ".log";
    auto makeCommand = tempFileName + script_ext + " > " + makeOutput + " 2>&1";

#ifndef STMDSP_WIN32
    system((std::string("chmod +x ") + tempFileName + script_ext).c_str());
#endif
    int result = system(makeCommand.c_str());
    std::ifstream result_file (makeOutput);
    std::ostringstream sstr;
    sstr << result_file.rdbuf();
    log(sstr.str().c_str());

    std::filesystem::remove(tempFileName);
    std::filesystem::remove(tempFileName + script_ext);
    std::filesystem::remove(makeOutput);

    if (result == 0) {
        editorCompiled = editor.GetText();
        log("Compilation succeeded.");
    } else {
        log("Compilation failed.");
    }
}

void disassembleCode()
{
    log("Disassembling...");

    if (tempFileName.size() == 0 || editor.GetText().compare(editorCompiled) != 0) {
        compileEditorCode();
    }

    auto output = tempFileName + ".asm.log";
    auto command = std::string("arm-none-eabi-objdump -d --no-show-raw-insn ") +
        tempFileName + ".orig.o > " + output + " 2>&1";
    
    if (system(command.c_str()) == 0) {
        {
            std::ifstream result_file (output);
            std::ostringstream sstr;
            sstr << result_file.rdbuf();
            log(sstr.str().c_str());
        }

        ImGui::OpenPopup("compile");
        std::filesystem::remove(output);

        log("Ready.");
    } else {
        log("Failed to load disassembly.");
    }
}

std::string newTempFileName()
{
    auto tempPath = std::filesystem::temp_directory_path();
    tempPath /= "stmdspgui_build";
    return tempPath.string();
}

