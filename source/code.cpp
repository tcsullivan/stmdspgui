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
static bool codeExecuteCommand(const std::string& command, const std::string& file);
static void stringReplaceAll(std::string& str, const std::string& what, const std::string& with);
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

std::string newTempFileName()
{
    const auto path = std::filesystem::temp_directory_path() / "stmdspgui_build";
    return path.string();
}

bool codeExecuteCommand(const std::string& command, const std::string& file)
{
    if (system(command.c_str()) == 0) {
        if (std::ifstream output (file); output.good()) {
            std::ostringstream sstr;
            sstr << output.rdbuf();
            log(sstr.str().c_str());
        } else {
            log("Could not read command output!");
        }

        std::filesystem::remove(file);
        return true;
    } else {
        return false;
    }
}

void stringReplaceAll(std::string& str, const std::string& what, const std::string& with)
{
    std::size_t i;
    while ((i = str.find(what)) != std::string::npos) {
        str.replace(i, what.size(), with);
        i += what.size();
    }
};

void compileEditorCode()
{
    log("Compiling...");

    // Scrap cached build if there are changes
    if (editor.GetText().compare(editorCompiled) != 0) {
        std::filesystem::remove(tempFileName + ".o");
        std::filesystem::remove(tempFileName + ".orig.o");
    }

    const auto platform = m_device ? m_device->get_platform()
                                   : stmdsp::platform::L4;

    if (tempFileName.empty())
        tempFileName = newTempFileName();


    {
        std::ofstream file (tempFileName, std::ios::trunc | std::ios::binary);

        auto file_text =
            platform == stmdsp::platform::L4 ? stmdsp::file_header_l4
                                             : stmdsp::file_header_h7;
        const auto buffer_size = m_device ? m_device->get_buffer_size()
                                          : stmdsp::SAMPLES_MAX;

        stringReplaceAll(file_text, "$0", std::to_string(buffer_size));

        file << file_text << '\n' << editor.GetText();
    }

    const auto scriptFile = tempFileName +
#ifndef STMDSP_WIN32
        ".sh";
#else
        ".bat";
#endif

    {
        std::ofstream makefile (scriptFile, std::ios::binary);
        auto make_text =
            platform == stmdsp::platform::L4 ? stmdsp::makefile_text_l4
                                             : stmdsp::makefile_text_h7;

        stringReplaceAll(make_text, "$0", tempFileName);
        stringReplaceAll(make_text, "$1",
                         std::filesystem::current_path().string());

        makefile << make_text;
    }

#ifndef STMDSP_WIN32
    system((std::string("chmod +x ") + scriptFile).c_str());
#endif

    const auto makeOutput = scriptFile + ".log";
    const auto makeCommand = scriptFile + " > " + makeOutput + " 2>&1";
    if (codeExecuteCommand(makeCommand, makeOutput)) {
        editorCompiled = editor.GetText();
        log("Compilation succeeded.");
    } else {
        log("Compilation failed.");
    }

    std::filesystem::remove(tempFileName);
    std::filesystem::remove(scriptFile);
}

void disassembleCode()
{
    log("Disassembling...");

    if (tempFileName.size() == 0 || editor.GetText().compare(editorCompiled) != 0) {
        compileEditorCode();
    }

    const auto output = tempFileName + ".asm.log";
    const auto command =
        std::string("arm-none-eabi-objdump -d --no-show-raw-insn ") +
        tempFileName + ".orig.o > " + output + " 2>&1";

    if (codeExecuteCommand(command, output)) {
        log("Ready.");
    } else {
        log("Failed to load disassembly.");
    }
}

