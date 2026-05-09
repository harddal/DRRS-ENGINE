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

// ---- AngelScript language, palette, and autocomplete -----------------------

static const TextEditor::Language* ascLanguage()
{
    static bool initialized = false;
    static TextEditor::Language lang;

    if (!initialized) {
        lang = *TextEditor::Language::AngelScript();

        lang.keywords.clear();
        for (const char* k : { "if", "else", "void", "int8", "return", "int16", "int", "int64",
                                "uint8", "uint16", "uint", "uint64", "float", "double", "bool",
                                "ref", "weakref", "self", "true", "false", "const", "for", "while", 
								"export" })
            lang.keywords.insert(k);

        lang.declarations.clear();
        for (const char* k : { "string", "dictionary", "entityid", "itemid", "vector2d", "vector3d" })
            lang.declarations.insert(k);

        lang.identifiers.clear();
        for (const char* k : { "debug", "log", "math", "time", "transform", "entity", "camera",
                                "light", "physx", "render", "animation", "raycast", "postprocess",
                                "sound", "game", "player", "cvar", "console", "autokill", "interaction",
                                "logic", "trigger", "marker", "hp", "data", "item", "dialog", "water",
                                "npc", "tween", "nav", "input" })
            lang.identifiers.insert(k);

        initialized = true;
    }
    return &lang;
}

static const TextEditor::Palette& ascPalette()
{
    static const TextEditor::Palette p = []() {
        TextEditor::Palette p = TextEditor::GetDarkPalette();
        p[static_cast<size_t>(TextEditor::Color::text)]						 = IM_COL32(0xC0, 0xC0, 0xC0, 255);
        p[static_cast<size_t>(TextEditor::Color::keyword)]					 = IM_COL32(0x24, 0x92, 0xFF, 255);
        p[static_cast<size_t>(TextEditor::Color::declaration)]				 = IM_COL32(0xFF, 0x2B, 0x95, 255);
        p[static_cast<size_t>(TextEditor::Color::number)]					 = IM_COL32(0x80, 0xFF, 0x80, 255);
        p[static_cast<size_t>(TextEditor::Color::string)]					 = IM_COL32(0xFF, 0x80, 0x40, 255);
        p[static_cast<size_t>(TextEditor::Color::punctuation)]				 = IM_COL32(0xC0, 0xC0, 0xC0, 255);
        p[static_cast<size_t>(TextEditor::Color::preprocessor)]				 = IM_COL32(0x40, 0xD0, 0xA0, 255);
        p[static_cast<size_t>(TextEditor::Color::identifier)]				 = IM_COL32(0xC0, 0xC0, 0xC0, 255);
        p[static_cast<size_t>(TextEditor::Color::knownIdentifier)]			 = IM_COL32(0x80, 0x80, 0xFF, 255);
        p[static_cast<size_t>(TextEditor::Color::comment)]					 = IM_COL32(0x00, 0x80, 0x00, 255);
        p[static_cast<size_t>(TextEditor::Color::background)]				 = IM_COL32(0x1E, 0x1E, 0x1E, 255);
        p[static_cast<size_t>(TextEditor::Color::cursor)]					 = IM_COL32(0xE0, 0xE0, 0xE0, 255);
        p[static_cast<size_t>(TextEditor::Color::selection)]				 = IM_COL32(0x20, 0x60, 0xA0, 255);
        p[static_cast<size_t>(TextEditor::Color::whitespace)]				 = IM_COL32(0x50, 0x50, 0x50, 255);
        p[static_cast<size_t>(TextEditor::Color::matchingBracketBackground)] = IM_COL32(0x46, 0x46, 0x46, 255);
        p[static_cast<size_t>(TextEditor::Color::matchingBracketActive)]	 = IM_COL32(0x8C, 0x8C, 0x8C, 255);
        p[static_cast<size_t>(TextEditor::Color::matchingBracketLevel1)]	 = IM_COL32(0xF6, 0xDE, 0x24, 255);
        p[static_cast<size_t>(TextEditor::Color::matchingBracketLevel2)]	 = IM_COL32(0x24, 0x92, 0xFF, 255);
        p[static_cast<size_t>(TextEditor::Color::matchingBracketLevel3)]	 = IM_COL32(0x80, 0x80, 0xFF, 255);
        p[static_cast<size_t>(TextEditor::Color::matchingBracketError)]		 = IM_COL32(0xC6, 0x08, 0x20, 255);
        p[static_cast<size_t>(TextEditor::Color::lineNumber)]				 = IM_COL32(0x80, 0x80, 0x90, 255);
        p[static_cast<size_t>(TextEditor::Color::currentLineNumber)]		 = IM_COL32(0xE0, 0xE0, 0xF0, 255);
        return p;
    }();
    return p;
}

static TextEditor::Trie               s_ascTrie;
static TextEditor::AutoCompleteConfig s_ascAutoComplete;

static void initAscAutoComplete()
{
    static bool initialized = false;
    if (initialized) return;

    const auto* lang = ascLanguage();
    for (auto& kw : lang->keywords)     s_ascTrie.insert(kw);
    for (auto& kw : lang->declarations) s_ascTrie.insert(kw);
    for (auto& kw : lang->identifiers)  s_ascTrie.insert(kw);

    std::ifstream funcFile("script_functions.txt");
    std::string line;
    while (std::getline(funcFile, line)) {
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string sig = line.substr(sp + 1);
        if (!sig.empty() && sig.back() == ';') sig.pop_back();
        if (!sig.empty()) s_ascTrie.insert(sig);
    }

    s_ascAutoComplete.triggerOnTyping   = true;
    s_ascAutoComplete.triggerInComments = false;
    s_ascAutoComplete.triggerInStrings  = false;
    s_ascAutoComplete.callback = [](TextEditor::AutoCompleteState& state) {
        if (state.searchTerm.empty()) return;
        s_ascTrie.findSuggestions(state.suggestions, state.searchTerm);
    };

    initialized = true;
}

static void applyLangSettings(ScriptTab& tab, const std::string& path)
{
    auto dot = path.rfind('.');
    std::string ext = dot != std::string::npos ? path.substr(dot) : "";

    if (ext == ".asc") {
        initAscAutoComplete();
        tab.editor.SetLanguage(ascLanguage());
        tab.editor.SetPalette(ascPalette());
        tab.editor.SetAutoCompleteConfig(&s_ascAutoComplete);
    } else if (ext == ".frag" || ext == ".vert") {
        tab.editor.SetLanguage(TextEditor::Language::Glsl());
        tab.editor.SetPalette(TextEditor::GetDarkPalette());
    } else {
        tab.editor.SetPalette(TextEditor::GetDarkPalette());
    }
}

// ---- helpers ----------------------------------------------------------------

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
    tab->editor.SetText(ss.str());
    applyLangSettings(*tab, path);
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
		tab->editor.SetShowWhitespacesEnabled(false);
		tab->editor.SetShowSpacesEnabled(false);
		tab->editor.SetShowTabsEnabled(false);

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
