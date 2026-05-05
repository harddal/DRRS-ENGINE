#include "EditorInterface_Internal.h"

#include <IMGUI/imgui.h>
#include "TextEditor/TextEditor.h"
#include "Utility/Utility.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#define dialog_filter_scripts \
    "All Supported\0*.asc;*.ent;*.frag;*.vert;*.anim\0" \
    "AngelScript\0*.asc\0" \
    "Entity\0*.ent\0" \
    "Fragment Shader\0*.frag\0" \
    "Vertex Shader\0*.vert\0" \
    "Animation\0*.anim\0" \
    "Any File\0*.*\0"

struct ScriptTab
{
    TextEditor  editor;
    std::string path;
    std::string name;
    size_t      savedUndoIndex = 0;

    bool isDirty() const { return editor.GetUndoIndex() != savedUndoIndex; }
};

static std::vector<std::unique_ptr<ScriptTab>> s_tabs;
static int  s_activeTab       = -1;
static int  s_tabPendingClose = -1;
static bool s_openModal       = false;
static bool s_initialized     = false;

// ---- helpers ----------------------------------------------------------------

static const TextEditor::Language* detectLanguage(const std::string& path)
{
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return nullptr;
    std::string ext = path.substr(dot);
    if (ext == ".frag" || ext == ".vert") return TextEditor::Language::Glsl();
    if (ext == ".asc")                    return TextEditor::Language::AngelScript();
    return nullptr;
}

static std::string extractName(const std::string& path)
{
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static ScriptTab* activeTab()
{
    if (s_activeTab >= 0 && s_activeTab < (int)s_tabs.size())
        return s_tabs[s_activeTab].get();
    return nullptr;
}

// ---- file operations --------------------------------------------------------

static void newTab()
{
    auto tab = std::make_unique<ScriptTab>();
    tab->name = "untitled";
    tab->editor.SetPalette(TextEditor::GetDarkPalette());
    s_tabs.push_back(std::move(tab));
    s_activeTab = (int)s_tabs.size() - 1;
}

static void openFile(const std::string& path)
{
    for (int i = 0; i < (int)s_tabs.size(); i++)
    {
        if (s_tabs[i]->path == path) { s_activeTab = i; return; }
    }

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::stringstream ss;
    ss << file.rdbuf();

    auto tab = std::make_unique<ScriptTab>();
    tab->path = path;
    tab->name = extractName(path);
    tab->editor.SetPalette(TextEditor::GetDarkPalette());
    tab->editor.SetText(ss.str());
    if (auto* lang = detectLanguage(path)) tab->editor.SetLanguage(lang);
    tab->savedUndoIndex = tab->editor.GetUndoIndex();

    s_tabs.push_back(std::move(tab));
    s_activeTab = (int)s_tabs.size() - 1;
}

static void saveTab(int index)
{
    if (index < 0 || index >= (int)s_tabs.size()) return;
    auto& tab = *s_tabs[index];

    if (tab.path.empty())
    {
        std::string path = Utility::SaveFileDialog(dialog_filter_scripts, "content\\script");
        if (path.empty()) return;
        tab.path = path;
        tab.name = extractName(path);
    }

    std::ofstream file(tab.path);
    if (!file.is_open()) return;
    file << tab.editor.GetText();
    tab.savedUndoIndex = tab.editor.GetUndoIndex();
}

static void saveTabAs(int index)
{
    if (index < 0 || index >= (int)s_tabs.size()) return;
    auto& tab = *s_tabs[index];

    std::string path = Utility::SaveFileDialog(dialog_filter_scripts, "content\\script");
    if (path.empty()) return;
    tab.path = path;
    tab.name = extractName(path);

    std::ofstream file(tab.path);
    if (!file.is_open()) return;
    file << tab.editor.GetText();
    tab.savedUndoIndex = tab.editor.GetUndoIndex();
}

// ---- draw -------------------------------------------------------------------

void EditorInterface::draw_window_script_editor()
{
    if (!m_windowData.draw_window_script_editor)
        return;

    if (!s_initialized)
    {
        newTab();
        s_initialized = true;
    }

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Script Editor", &m_windowData.draw_window_script_editor, ImGuiWindowFlags_MenuBar))
    {
        ImGui::End();
        return;
    }

    // ---- menu bar -----------------------------------------------------------
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N"))
                newTab();

            if (ImGui::MenuItem("Open...", "Ctrl+O"))
            {
                std::string path = Utility::OpenFileDialog(dialog_filter_scripts, "content\\script");
                if (!path.empty()) openFile(path);
            }

            ImGui::Separator();

            bool hasTab = activeTab() != nullptr;
            if (ImGui::MenuItem("Save", "Ctrl+S", nullptr, hasTab))
                saveTab(s_activeTab);
            if (ImGui::MenuItem("Save As...", nullptr, nullptr, hasTab))
                saveTabAs(s_activeTab);

            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // ---- keyboard shortcuts (only when this window is focused) --------------
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newTab();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
        {
            std::string path = Utility::OpenFileDialog(dialog_filter_scripts);
            if (!path.empty()) openFile(path);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            saveTab(s_activeTab);
    }

    // ---- tab bar ------------------------------------------------------------
    if (ImGui::BeginTabBar("##scripttabs", ImGuiTabBarFlags_Reorderable))
    {
        for (int i = 0; i < (int)s_tabs.size(); i++)
        {
            auto& tab = *s_tabs[i];
            ImGuiTabItemFlags flags = tab.isDirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
            bool open = true;

            if (ImGui::BeginTabItem(tab.name.c_str(), &open, flags))
            {
                s_activeTab = i;
                ImGui::EndTabItem();
            }

            if (!open)
            {
                if (s_tabs[i]->isDirty())
                {
                    s_tabPendingClose = i;
                    s_openModal = true;
                }
                else
                {
                    s_tabs.erase(s_tabs.begin() + i);
                    if (s_activeTab >= (int)s_tabs.size())
                        s_activeTab = (int)s_tabs.size() - 1;
                    i--;
                }
            }
        }

        ImGui::EndTabBar();
    }

    // ---- active editor ------------------------------------------------------
    if (auto* tab = activeTab())
    {
        ImFont* monoFont = ImGui::GetIO().Fonts->Fonts.Size > 1
            ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;
        ImGui::PushFont(monoFont);
        tab->editor.Render("##scripteditor", ImGui::GetContentRegionAvail());
        ImGui::PopFont();
    }

    ImGui::End();
}

void drawScriptEditorPopups()
{
    if (s_tabPendingClose < 0) return;

    // Host the modal in a top-level invisible window so it renders above
    // all docked windows and the scene viewport.
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##se_modal_host", nullptr,
        ImGuiWindowFlags_NoDecoration  | ImGuiWindowFlags_NoNav         |
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);

    if (s_openModal)
    {
        ImGui::OpenPopup("Unsaved Changes##se");
        s_openModal = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Changes##se", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const std::string& name = s_tabs[s_tabPendingClose]->name;
        ImGui::Text("'%s' has unsaved changes.", name.c_str());
        ImGui::Text("Do you want to save before closing?");
        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(90, 0)))
        {
            saveTab(s_tabPendingClose);
            s_tabs.erase(s_tabs.begin() + s_tabPendingClose);
            if (s_activeTab >= (int)s_tabs.size())
                s_activeTab = (int)s_tabs.size() - 1;
            s_tabPendingClose = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(90, 0)))
        {
            s_tabs.erase(s_tabs.begin() + s_tabPendingClose);
            if (s_activeTab >= (int)s_tabs.size())
                s_activeTab = (int)s_tabs.size() - 1;
            s_tabPendingClose = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            s_tabPendingClose = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}
