#include "Engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace {
    static uint64_t hashBuffer(const std::string& text) {
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char c : text) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static std::string trimLeft(const std::string& value) {
        size_t start = value.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        return value.substr(start);
    }

    static std::vector<std::string> buildSymbolList(const std::string& text) {
        std::vector<std::string> symbols;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            std::string trimmed = trimLeft(line);
            if (trimmed.empty()) continue;
            if (trimmed.rfind("//", 0) == 0) continue;

            auto captureToken = [&](const std::string& prefix) -> bool {
                if (trimmed.rfind(prefix, 0) != 0) return false;
                size_t start = prefix.size();
                while (start < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[start]))) {
                    ++start;
                }
                size_t end = start;
                while (end < trimmed.size() &&
                       (std::isalnum(static_cast<unsigned char>(trimmed[end])) || trimmed[end] == '_' || trimmed[end] == ':')) {
                    ++end;
                }
                if (end > start) {
                    symbols.emplace_back(trimmed.substr(0, end));
                    return true;
                }
                return false;
            };

            if (captureToken("class ") || captureToken("struct ") || captureToken("enum ") || captureToken("namespace ")) {
                continue;
            }

            if (trimmed.find('(') != std::string::npos && trimmed.find(')') != std::string::npos &&
                (trimmed.find('{') != std::string::npos || trimmed.back() == ';')) {
                static const char* kSkip[] = {"if", "for", "while", "switch", "catch"};
                bool skip = false;
                for (const char* keyword : kSkip) {
                    if (trimmed.rfind(keyword, 0) == 0) {
                        skip = true;
                        break;
                    }
                }
                if (skip) continue;
                size_t paren = trimmed.find('(');
                if (paren != std::string::npos && paren > 0) {
                    size_t end = paren;
                    while (end > 0 && std::isspace(static_cast<unsigned char>(trimmed[end - 1]))) {
                        --end;
                    }
                    size_t start = end;
                    while (start > 0 &&
                           (std::isalnum(static_cast<unsigned char>(trimmed[start - 1])) || trimmed[start - 1] == '_' ||
                            trimmed[start - 1] == ':')) {
                        --start;
                    }
                    if (end > start) {
                        symbols.emplace_back(trimmed.substr(start, paren - start));
                    }
                }
            }
        }
        return symbols;
    }

    static std::vector<std::string> buildCompletionList(const std::vector<std::string>& pool,
                                                        const std::string& prefix,
                                                        size_t limit = 16) {
        std::vector<std::string> matches;
        if (prefix.empty()) return matches;
        for (const auto& entry : pool) {
            if (entry.rfind(prefix, 0) == 0) {
                matches.push_back(entry);
                if (matches.size() >= limit) break;
            }
        }
        return matches;
    }

    static const std::unordered_set<std::string>& cppKeywordSet() {
        static const std::unordered_set<std::string> kKeywords = {
            "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr", "continue",
            "default", "delete", "do", "double", "else", "enum", "explicit", "extern", "false", "float",
            "for", "friend", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
            "operator", "private", "protected", "public", "return", "short", "signed", "sizeof", "static",
            "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typename",
            "union", "unsigned", "using", "virtual", "void", "volatile", "while"
        };
        return kKeywords;
    }

    static std::vector<std::string> extractIdentifiers(const std::string& text) {
        std::unordered_set<std::string> unique;
        const auto& keywords = cppKeywordSet();
        std::string token;
        token.reserve(64);
        auto flushToken = [&]() {
            if (token.size() >= 2 && keywords.find(token) == keywords.end()) {
                unique.insert(token);
            }
            token.clear();
        };
        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                token.push_back(c);
            } else if (!token.empty()) {
                flushToken();
            }
        }
        if (!token.empty()) {
            flushToken();
        }
        std::vector<std::string> out(unique.begin(), unique.end());
        std::sort(out.begin(), out.end());
        return out;
    }

}

void Engine::refreshScriptingFileList() {
    scriptingFileList.clear();
    scriptingCompletions.clear();
    if (!projectManager.currentProject.isLoaded) {
        return;
    }

    fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);
    ScriptBuildConfig config;
    std::string error;
    if (!scriptCompiler.loadConfig(configPath, config, error)) {
        return;
    }
    fs::path scriptsRoot = config.scriptsDir;
    if (!scriptsRoot.is_absolute()) {
        scriptsRoot = projectManager.currentProject.projectPath / scriptsRoot;
    }
    std::error_code ec;
    if (!fs::exists(scriptsRoot, ec)) {
        return;
    }

    const std::unordered_set<std::string> validExt = {
        ".cpp", ".cc", ".cxx", ".c", ".hpp", ".h", ".inl"
    };

    for (auto it = fs::recursive_directory_iterator(scriptsRoot, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_directory()) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (validExt.find(ext) == validExt.end()) continue;
        scriptingFileList.push_back(it->path());
    }

    std::sort(scriptingFileList.begin(), scriptingFileList.end());

    std::unordered_set<std::string> uniqueSymbols;
    for (const auto& scriptPath : scriptingFileList) {
        std::ifstream file(scriptPath);
        if (!file.is_open()) continue;
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::vector<std::string> symbols = buildSymbolList(buffer.str());
        for (auto& symbol : symbols) {
            uniqueSymbols.insert(symbol);
        }
    }
    scriptingCompletions.assign(uniqueSymbols.begin(), uniqueSymbols.end());
    std::sort(scriptingCompletions.begin(), scriptingCompletions.end());
}

void Engine::openScriptInEditor(const fs::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    fs::path absPath = fs::absolute(path, ec);
    fs::path normalized = (ec ? path : absPath).lexically_normal();

    std::ifstream file(normalized);
    std::stringstream buffer;
    if (file.is_open()) {
        buffer << file.rdbuf();
    }
    scriptEditorState.filePath = normalized;
    scriptEditorState.buffer = buffer.str();
    if (!scriptTextEditorReady) {
        auto lang = TextEditor::LanguageDefinition::CPlusPlus();
        lang.mIdentifiers.insert({"Begin", {}});
        lang.mIdentifiers.insert({"TickUpdate", {}});
        lang.mIdentifiers.insert({"Spec", {}});
        lang.mIdentifiers.insert({"TestEditor", {}});
        lang.mIdentifiers.insert({"Update", {}});
        scriptTextEditor.SetLanguageDefinition(lang);
        auto palette = scriptTextEditor.GetPalette();
        palette[(int)TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(220, 180, 70, 255);
        scriptTextEditor.SetPalette(palette);
        scriptTextEditor.SetShowWhitespaces(true);
        scriptTextEditor.SetAllowTabInput(false);
        scriptTextEditor.SetSmartTabDelete(true);
        scriptTextEditorReady = true;
    }
    scriptTextEditor.SetText(scriptEditorState.buffer);
    scriptEditorState.dirty = false;
    scriptEditorState.hasWriteTime = false;
    if (fs::exists(normalized, ec)) {
        scriptEditorState.lastWriteTime = fs::last_write_time(normalized, ec);
        scriptEditorState.hasWriteTime = !ec;
    }
    showScriptingWindow = true;
}

void Engine::renderScriptingWindow() {
    if (!showScriptingWindow) return;

    if (scriptingFilesDirty) {
        refreshScriptingFileList();
        scriptingFilesDirty = false;
    }

    ImGui::Begin("Scripting", &showScriptingWindow);
    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("Load a project to edit scripts.");
        ImGui::End();
        return;
    }

    static std::vector<std::string> symbols;
    static uint64_t symbolsHash = 0;
    static std::vector<std::string> bufferIdentifiers;
    static uint64_t identifiersHash = 0;
    static std::vector<std::string> completionPool;
    static std::vector<std::string> activeSuggestions;
    static std::string activePrefix;
    static bool completionPanelOpen = true;

    ImGui::TextDisabled("C++ Script Editor");
    ImGui::SameLine();
    if (ImGui::Button("Refresh List")) {
        scriptingFilesDirty = true;
    }

    ImGui::Separator();

    float leftWidth = 240.0f;
    ImGui::BeginChild("ScriptingFiles", ImVec2(leftWidth, 0.0f), true);
    ImGui::TextDisabled("Scripts");
    ImGui::InputTextWithHint("##ScriptFilter", "Filter", scriptingFilter, sizeof(scriptingFilter));
    ImGui::Separator();

    for (const auto& scriptPath : scriptingFileList) {
        std::string label = scriptPath.filename().string();
        std::string filter = scriptingFilter;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
        std::string lowerLabel = label;
        std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), ::tolower);
        if (!filter.empty() && lowerLabel.find(filter) == std::string::npos) {
            continue;
        }
        bool selected = (scriptEditorState.filePath == scriptPath);
        if (ImGui::Selectable(label.c_str(), selected)) {
            openScriptInEditor(scriptPath);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ScriptingEditor", ImVec2(0.0f, 0.0f), false);
    ImGui::TextDisabled("Active File");
    ImGui::SameLine();
    std::string fileLabel = scriptEditorState.filePath.empty()
        ? std::string("None")
        : scriptEditorState.filePath.filename().string();
    ImGui::TextUnformatted(fileLabel.c_str());

    bool hasFile = !scriptEditorState.filePath.empty();
    ImGui::SameLine();
    if (!hasFile) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save")) {
        std::ofstream out(scriptEditorState.filePath);
        if (out.is_open()) {
            scriptEditorState.buffer = scriptTextEditor.GetText();
            out << scriptEditorState.buffer;
            scriptEditorState.dirty = false;
            std::error_code ec;
            scriptEditorState.lastWriteTime = fs::last_write_time(scriptEditorState.filePath, ec);
            scriptEditorState.hasWriteTime = !ec;
            if (scriptEditorState.autoCompileOnSave) {
                compileScriptFile(scriptEditorState.filePath);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Compile")) {
        compileScriptFile(scriptEditorState.filePath);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-compile on save", &scriptEditorState.autoCompileOnSave);
    if (!hasFile) {
        ImGui::EndDisabled();
    }

    bool canReload = false;
    if (hasFile && scriptEditorState.hasWriteTime) {
        std::error_code ec;
        if (fs::exists(scriptEditorState.filePath, ec)) {
            auto diskTime = fs::last_write_time(scriptEditorState.filePath, ec);
            if (!ec && diskTime > scriptEditorState.lastWriteTime) {
                canReload = true;
            }
        }
    }
    if (canReload) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "File changed on disk");
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            openScriptInEditor(scriptEditorState.filePath);
        }
    }

    ImGui::Separator();

        if (ImGui::BeginTabBar("ScriptingTabs")) {
            if (ImGui::BeginTabItem("Editor")) {
                if (hasFile) {
                    if (!scriptTextEditorReady) {
                        auto lang = TextEditor::LanguageDefinition::CPlusPlus();
                        lang.mIdentifiers.insert({"Begin", {}});
                        lang.mIdentifiers.insert({"TickUpdate", {}});
                        lang.mIdentifiers.insert({"Spec", {}});
                        lang.mIdentifiers.insert({"TestEditor", {}});
                        lang.mIdentifiers.insert({"Update", {}});
                        scriptTextEditor.SetLanguageDefinition(lang);
                        auto palette = scriptTextEditor.GetPalette();
                        palette[(int)TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(220, 180, 70, 255);
                        scriptTextEditor.SetPalette(palette);
                        scriptTextEditor.SetShowWhitespaces(true);
                        scriptTextEditor.SetAllowTabInput(false);
                        scriptTextEditor.SetSmartTabDelete(true);
                        scriptTextEditorReady = true;
                    }
                completionPool.clear();
                std::unordered_set<std::string> poolSet;
                for (const auto& kw : cppKeywordSet()) {
                    poolSet.insert(kw);
                }
                for (const auto& entry : scriptingCompletions) {
                    poolSet.insert(entry);
                }
                for (const auto& entry : symbols) {
                    poolSet.insert(entry);
                }
                uint64_t bufferHash = hashBuffer(scriptEditorState.buffer);
                if (bufferHash != identifiersHash) {
                    identifiersHash = bufferHash;
                    bufferIdentifiers = extractIdentifiers(scriptEditorState.buffer);
                }
                for (const auto& entry : bufferIdentifiers) {
                    poolSet.insert(entry);
                }
                completionPool.assign(poolSet.begin(), poolSet.end());
                std::sort(completionPool.begin(), completionPool.end());

                    TextEditor::Coordinates cursorBefore = scriptTextEditor.GetCursorPosition();
                    activePrefix = scriptTextEditor.GetWordAtPublic(cursorBefore);
                    if (activePrefix.empty() && cursorBefore.mColumn > 0) {
                        TextEditor::Coordinates prev(cursorBefore.mLine, cursorBefore.mColumn - 1);
                        activePrefix = scriptTextEditor.GetWordAtPublic(prev);
                    }
                    if (!activePrefix.empty() && activePrefix.size() >= 2) {
                        activeSuggestions = buildCompletionList(completionPool, activePrefix);
                    } else {
                        activeSuggestions.clear();
                    }

                    bool tabPressed = ImGui::IsKeyPressed(ImGuiKey_Tab);
                    bool canComplete = !activeSuggestions.empty() && !ImGui::GetIO().KeyShift;
                    scriptTextEditor.SetAllowTabInput(!canComplete);

                    float completionHeight = completionPanelOpen ? 140.0f : 0.0f;
                    float availHeight = ImGui::GetContentRegionAvail().y;
                    float editorHeight = std::max(120.0f, availHeight - completionHeight - 12.0f);
                    ImVec2 editorSize = ImVec2(0.0f, editorHeight);
                    scriptTextEditor.Render("##ScriptEditor", editorSize, false);
                    if (scriptTextEditor.IsTextChanged()) {
                        scriptEditorState.dirty = true;
                        scriptEditorState.buffer = scriptTextEditor.GetText();
                    }
                    uint64_t newHash = hashBuffer(scriptEditorState.buffer);
                    if (newHash != symbolsHash) {
                        symbolsHash = newHash;
                        symbols = buildSymbolList(scriptEditorState.buffer);
                    }

                    TextEditor::Coordinates cursorAfter = scriptTextEditor.GetCursorPosition();
                    activePrefix = scriptTextEditor.GetWordAtPublic(cursorAfter);
                    if (activePrefix.empty() && cursorAfter.mColumn > 0) {
                        TextEditor::Coordinates prev(cursorAfter.mLine, cursorAfter.mColumn - 1);
                        activePrefix = scriptTextEditor.GetWordAtPublic(prev);
                    }
                    if (!activePrefix.empty() && activePrefix.size() >= 2) {
                        activeSuggestions = buildCompletionList(completionPool, activePrefix);
                    } else {
                        activeSuggestions.clear();
                    }
                    bool canCompleteNow = !activeSuggestions.empty() && !ImGui::GetIO().KeyShift;

                    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                        tabPressed && canCompleteNow) {
                        TextEditor::Coordinates cursor = scriptTextEditor.GetCursorPosition();
                        TextEditor::Coordinates start(cursor.mLine,
                            std::max(0, cursor.mColumn - static_cast<int>(activePrefix.size())));
                        scriptTextEditor.SetSelection(start, cursor);
                        scriptTextEditor.Delete();
                        scriptTextEditor.InsertText(activeSuggestions.front().c_str());
                        scriptEditorState.dirty = true;
                        scriptEditorState.buffer = scriptTextEditor.GetText();
                    }

                    if (completionPanelOpen) {
                        ImGui::Separator();
                        ImGui::TextDisabled("Completions");
                        ImGui::SameLine();
                        ImGui::Checkbox("Show", &completionPanelOpen);
                        ImGui::BeginChild("CompletionList", ImVec2(0.0f, completionHeight), true);
                        if (activeSuggestions.empty()) {
                            ImGui::TextDisabled("No suggestions");
                        } else {
                            for (const auto& suggestion : activeSuggestions) {
                                if (ImGui::Selectable(suggestion.c_str())) {
                                    TextEditor::Coordinates cursor = scriptTextEditor.GetCursorPosition();
                                    TextEditor::Coordinates start(cursor.mLine,
                                        std::max(0, cursor.mColumn - static_cast<int>(activePrefix.size())));
                                    scriptTextEditor.SetSelection(start, cursor);
                                    scriptTextEditor.Delete();
                                    scriptTextEditor.InsertText(suggestion.c_str());
                                    scriptEditorState.dirty = true;
                                    scriptEditorState.buffer = scriptTextEditor.GetText();
                                }
                            }
                        }
                        ImGui::EndChild();
                    }

                    if (canCompleteNow && scriptTextEditor.HasCursorScreenPosition()) {
                        const std::string& suggestion = activeSuggestions.front();
                        if (suggestion.size() > activePrefix.size() &&
                            suggestion.rfind(activePrefix, 0) == 0) {
                            std::string ghost = suggestion.substr(activePrefix.size());
                            ImVec2 ghostPos = scriptTextEditor.GetCursorScreenPositionPublic();
                            ImU32 ghostColor = IM_COL32(180, 180, 180, 110);
                            ImGui::GetWindowDrawList()->AddText(ghostPos, ghostColor, ghost.c_str());
                        }
                    }
                } else {
                    ImGui::TextDisabled("Select a script file to start editing.");
                }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Intellisense")) {
            ScriptBuildConfig config;
            std::string error;
            fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);
            bool hasConfig = scriptCompiler.loadConfig(configPath, config, error);
            if (hasConfig) {
                packageManager.applyToBuildConfig(config);
                ImGui::TextDisabled("Compiler");
                ImGui::Text("Standard: %s", config.cppStandard.c_str());
                ImGui::Separator();
                ImGui::TextDisabled("Include Dirs");
                for (const auto& includeDir : config.includeDirs) {
                    ImGui::BulletText("%s", includeDir.string().c_str());
                }
                ImGui::Separator();
                ImGui::TextDisabled("Defines");
                for (const auto& def : config.defines) {
                    ImGui::BulletText("%s", def.c_str());
                }
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.55f, 1.0f), "Scripts.modu not loaded");
                if (!error.empty()) {
                    ImGui::TextWrapped("%s", error.c_str());
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Outline");
            if (!symbols.empty()) {
                for (const auto& symbol : symbols) {
                    ImGui::BulletText("%s", symbol.c_str());
                }
            } else {
                ImGui::TextDisabled("No symbols detected.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Build")) {
            ImGui::TextDisabled("Compile Status");
            ImGui::Text("%s", lastCompileStatus.c_str());
            if (!lastCompileLog.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Output");
                ImGui::BeginChild("CompileLog", ImVec2(0.0f, 0.0f), true);
                ImGui::TextUnformatted(lastCompileLog.c_str());
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::End();
}
