#ifndef LOGVIEW_H
#define LOGVIEW_H

#include <string>

// Adapted from ExampleAppLog from imgui_demo.cpp
class LogView
{
public:
    LogView()
    {
        Clear();
    }

    void Clear()
    {
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0);
    }

    void AddLog(const std::string& str)
    {
        AddLog(str.c_str());
    }

    void AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        Buf.appendfv("\n", args);
        va_end(args);
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n')
                LineOffsets.push_back(old_size + 1);
    }

    void Draw(const char* title, bool* p_open = NULL, ImGuiWindowFlags flags = 0)
    {
        if (!ImGui::Begin(title, p_open, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Log ");
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            Clear();
        ImGui::SameLine();
        if (ImGui::Button("Copy"))
            ImGui::LogToClipboard();
        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);


        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        const char* buf = Buf.begin();
        const char* buf_end = Buf.end();
        ImGuiListClipper clipper;
        clipper.Begin(LineOffsets.Size);
        while (clipper.Step())
        {
            for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
            {
                const char* line_start = buf + LineOffsets[line_no];
                const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                ImGui::TextUnformatted(line_start, line_end);
            }
        }
        clipper.End();

        ImGui::PopStyleVar();

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }

private:
    ImGuiTextBuffer Buf;
    ImVector<int> LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
};

#endif // LOGVIEW_H

