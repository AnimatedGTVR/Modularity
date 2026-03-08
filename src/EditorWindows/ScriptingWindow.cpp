#include "Engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace {
    enum class ScriptEditorLanguage {
        Cpp,
        C,
        GLSL,
        HLSL,
        Lua
    };

    static std::string trimLeft(const std::string& value);

    static uint64_t hashBuffer(const std::string& text) {
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char c : text) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static std::string toLowerCopy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static std::string extensionLower(const fs::path& path) {
        return toLowerCopy(path.extension().string());
    }

    static bool isIdentifierStart(unsigned char c) {
        return std::isalpha(c) || c == '_';
    }

    static bool isIdentifierBody(unsigned char c) {
        return std::isalnum(c) || c == '_';
    }

    static ScriptEditorLanguage detectScriptEditorLanguage(const fs::path& path) {
        std::string ext = extensionLower(path);
        if (ext == ".c") return ScriptEditorLanguage::C;
        if (ext == ".glsl" || ext == ".vert" || ext == ".frag") return ScriptEditorLanguage::GLSL;
        if (ext == ".hlsl" || ext == ".shader") return ScriptEditorLanguage::HLSL;
        if (ext == ".lua") return ScriptEditorLanguage::Lua;
        return ScriptEditorLanguage::Cpp;
    }

    static bool isCompilableScriptPath(const fs::path& path) {
        const std::string ext = extensionLower(path);
        return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".cs" || ext == ".csproj";
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

    static const std::unordered_set<std::string>& cKeywordSet() {
        static const std::unordered_set<std::string> kKeywords = {
            "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
            "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return",
            "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
            "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
            "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
        };
        return kKeywords;
    }

    static const std::unordered_set<std::string>& glslKeywordSet() {
        static const std::unordered_set<std::string> kKeywords = {
            "attribute", "const", "uniform", "varying", "layout", "centroid", "flat", "smooth", "noperspective",
            "break", "continue", "do", "for", "while", "switch", "case", "default", "if", "else", "in", "out",
            "inout", "float", "double", "int", "void", "bool", "true", "false", "invariant", "discard", "return",
            "mat2", "mat3", "mat4", "dmat2", "dmat3", "dmat4", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
            "bvec2", "bvec3", "bvec4", "sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler2DShadow",
            "samplerCubeShadow", "sampler2DArray", "sampler2DArrayShadow", "isampler2D", "usampler2D", "struct",
            "precision", "highp", "mediump", "lowp", "uint"
        };
        return kKeywords;
    }

    static const std::unordered_set<std::string>& luaKeywordSet() {
        static const std::unordered_set<std::string> kKeywords = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
            "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
        };
        return kKeywords;
    }

    static const std::unordered_set<std::string>& keywordsForLanguage(ScriptEditorLanguage language) {
        switch (language) {
            case ScriptEditorLanguage::C:
                return cKeywordSet();
            case ScriptEditorLanguage::GLSL:
                return glslKeywordSet();
            case ScriptEditorLanguage::HLSL:
                return glslKeywordSet();
            case ScriptEditorLanguage::Lua:
                return luaKeywordSet();
            case ScriptEditorLanguage::Cpp:
            default:
                return cppKeywordSet();
        }
    }

    static TextEditor::LanguageDefinition baseLanguageDefinition(ScriptEditorLanguage language) {
        switch (language) {
            case ScriptEditorLanguage::C:
                return TextEditor::LanguageDefinition::C();
            case ScriptEditorLanguage::GLSL:
                return TextEditor::LanguageDefinition::GLSL();
            case ScriptEditorLanguage::HLSL:
                return TextEditor::LanguageDefinition::HLSL();
            case ScriptEditorLanguage::Lua:
                return TextEditor::LanguageDefinition::Lua();
            case ScriptEditorLanguage::Cpp:
            default:
                return TextEditor::LanguageDefinition::CPlusPlus();
        }
    }

    static std::string parseDefineName(const std::string& defineLine) {
        size_t i = 0;
        while (i < defineLine.size() && std::isspace(static_cast<unsigned char>(defineLine[i]))) {
            ++i;
        }
        if (i >= defineLine.size() || !isIdentifierStart(static_cast<unsigned char>(defineLine[i]))) {
            return {};
        }
        size_t start = i++;
        while (i < defineLine.size() && isIdentifierBody(static_cast<unsigned char>(defineLine[i]))) {
            ++i;
        }
        return defineLine.substr(start, i - start);
    }

    static std::vector<std::string> extractDefineIdentifiers(const std::string& text) {
        std::unordered_set<std::string> unique;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            std::string trimmed = trimLeft(line);
            if (trimmed.rfind("#define", 0) != 0) continue;
            std::string symbol = parseDefineName(trimmed.substr(7));
            if (!symbol.empty()) {
                unique.insert(symbol);
            }
        }
        std::vector<std::string> out(unique.begin(), unique.end());
        std::sort(out.begin(), out.end());
        return out;
    }

    static std::vector<std::string> extractFunctionIdentifiers(const std::string& text,
                                                               const std::unordered_set<std::string>& keywords) {
        static const std::unordered_set<std::string> kSkip = {
            "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "defined", "layout"
        };
        std::unordered_set<std::string> unique;
        size_t i = 0;
        while (i < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (!isIdentifierStart(c)) {
                ++i;
                continue;
            }
            size_t start = i++;
            while (i < text.size() && isIdentifierBody(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            std::string token = text.substr(start, i - start);
            size_t j = i;
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
                ++j;
            }
            if (j < text.size() && text[j] == '(' &&
                keywords.find(token) == keywords.end() &&
                kSkip.find(token) == kSkip.end()) {
                unique.insert(std::move(token));
            }
        }
        std::vector<std::string> out(unique.begin(), unique.end());
        std::sort(out.begin(), out.end());
        return out;
    }

    static void initializeScriptEditor(TextEditor& editor) {
        auto palette = editor.GetPalette();
        palette[(int)TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(220, 180, 70, 255);
        palette[(int)TextEditor::PaletteIndex::Preprocessor] = IM_COL32(110, 170, 220, 255);
        palette[(int)TextEditor::PaletteIndex::PreprocIdentifier] = IM_COL32(230, 165, 80, 255);
        editor.SetPalette(palette);
        editor.SetShowWhitespaces(true);
        editor.SetAllowTabInput(false);
        editor.SetSmartTabDelete(true);
    }

    static TextEditor::LanguageDefinition buildLanguageDefinition(ScriptEditorLanguage language,
                                                                  const std::vector<std::string>& functions,
                                                                  const std::vector<std::string>& defines) {
        TextEditor::LanguageDefinition lang = baseLanguageDefinition(language);
        std::unordered_set<std::string> defineSet(defines.begin(), defines.end());

        auto addIdentifier = [&](const std::string& name, const char* declaration) {
            if (name.empty()) return;
            TextEditor::Identifier id;
            id.mDeclaration = declaration;
            lang.mIdentifiers.insert({name, id});
        };

        if (language == ScriptEditorLanguage::Cpp || language == ScriptEditorLanguage::C) {
            addIdentifier("Begin", "Script callback");
            addIdentifier("TickUpdate", "Script callback");
            addIdentifier("Spec", "Script callback");
            addIdentifier("TestEditor", "Script callback");
            addIdentifier("Update", "Script callback");
        }

        for (const auto& name : functions) {
            if (defineSet.find(name) != defineSet.end()) continue;
            addIdentifier(name, "Function");
        }

        for (const auto& name : defines) {
            if (name.empty()) continue;
            TextEditor::Identifier id;
            id.mDeclaration = "Define";
            lang.mPreprocIdentifiers.insert({name, id});
        }

        return lang;
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

    static std::vector<std::string> extractIdentifiers(const std::string& text,
                                                       const std::unordered_set<std::string>& keywords) {
        std::unordered_set<std::string> unique;
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

    const std::unordered_set<std::string> validExt = {
        ".cpp", ".cc", ".cxx", ".c", ".hpp", ".h", ".inl",
        ".glsl", ".vert", ".frag", ".hlsl", ".shader", ".lua"
    };

    std::vector<fs::path> roots;
    fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);
    ScriptBuildConfig config;
    std::string error;
    if (scriptCompiler.loadConfig(configPath, config, error)) {
        fs::path scriptsRoot = config.scriptsDir;
        if (!scriptsRoot.is_absolute()) {
            scriptsRoot = projectManager.currentProject.projectPath / scriptsRoot;
        }
        roots.push_back(scriptsRoot);
    } else {
        roots.push_back(projectManager.currentProject.assetsPath / "Scripts");
    }
    roots.push_back(projectManager.currentProject.assetsPath / "Shaders");

    std::unordered_set<std::string> uniquePaths;
    for (const auto& root : roots) {
        std::error_code ec;
        if (root.empty() || !fs::exists(root, ec) || !fs::is_directory(root, ec)) {
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_directory()) continue;
            std::string ext = extensionLower(it->path());
            if (validExt.find(ext) == validExt.end()) continue;
            fs::path normalized = it->path().lexically_normal();
            std::string key = normalized.string();
            if (!uniquePaths.insert(key).second) continue;
            scriptingFileList.push_back(normalized);
        }
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
        initializeScriptEditor(scriptTextEditor);
        scriptTextEditorReady = true;
    }

    ScriptEditorLanguage language = detectScriptEditorLanguage(normalized);
    const auto& keywords = keywordsForLanguage(language);
    std::vector<std::string> functionIdentifiers = extractFunctionIdentifiers(scriptEditorState.buffer, keywords);
    std::vector<std::string> defineIdentifiers = extractDefineIdentifiers(scriptEditorState.buffer);
    scriptTextEditor.SetLanguageDefinition(buildLanguageDefinition(language, functionIdentifiers, defineIdentifiers));

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

    bool listRefreshedThisFrame = false;
    if (scriptingFilesDirty) {
        refreshScriptingFileList();
        scriptingFilesDirty = false;
        listRefreshedThisFrame = true;
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
    static std::vector<std::string> bufferFunctions;
    static std::vector<std::string> bufferDefines;
    static uint64_t identifiersHash = 0;
    static fs::path identifiersFilePath;
    static ScriptEditorLanguage activeLanguage = ScriptEditorLanguage::Cpp;
    static std::vector<std::string> completionPool;
    static std::vector<std::string> activeSuggestions;
    static std::string activePrefix;
    static bool completionPoolDirty = true;
    static bool completionPanelOpen = true;
    static fs::path reloadCheckPath;
    static bool cachedCanReload = false;
    static double lastReloadCheckTime = 0.0;
    static std::string cachedFilterLower;
    static std::vector<int> filteredScriptIndices;

    if (listRefreshedThisFrame) {
        completionPoolDirty = true;
    }

    ImGui::TextDisabled("Script & Shader Editor");
    ImGui::SameLine();
    if (ImGui::Button("Refresh List")) {
        scriptingFilesDirty = true;
    }

    ImGui::Separator();

    float leftWidth = 240.0f;
    ImGui::BeginChild("ScriptingFiles", ImVec2(leftWidth, 0.0f), true);
    ImGui::TextDisabled("Scripts / Shaders");
    bool filterChanged = ImGui::InputTextWithHint("##ScriptFilter", "Filter", scriptingFilter, sizeof(scriptingFilter));
    ImGui::Separator();

    std::string filterLower = toLowerCopy(scriptingFilter);
    if (listRefreshedThisFrame || filterChanged || filterLower != cachedFilterLower) {
        cachedFilterLower = filterLower;
        filteredScriptIndices.clear();
        filteredScriptIndices.reserve(scriptingFileList.size());
        for (size_t i = 0; i < scriptingFileList.size(); ++i) {
            if (!cachedFilterLower.empty()) {
                std::string labelLower = toLowerCopy(scriptingFileList[i].filename().string());
                if (labelLower.find(cachedFilterLower) == std::string::npos) {
                    continue;
                }
            }
            filteredScriptIndices.push_back(static_cast<int>(i));
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filteredScriptIndices.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const fs::path& scriptPath = scriptingFileList[filteredScriptIndices[row]];
            std::string label = scriptPath.filename().string();
            bool selected = (scriptEditorState.filePath == scriptPath);
            if (ImGui::Selectable(label.c_str(), selected)) {
                openScriptInEditor(scriptPath);
            }
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
    bool canCompileFile = hasFile && isCompilableScriptPath(scriptEditorState.filePath);
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
            cachedCanReload = false;
            lastReloadCheckTime = glfwGetTime();
            if (scriptEditorState.autoCompileOnSave && canCompileFile) {
                compileScriptFile(scriptEditorState.filePath);
            }
        }
    }
    ImGui::SameLine();
    if (!canCompileFile) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Compile")) {
        compileScriptFile(scriptEditorState.filePath);
    }
    if (!canCompileFile) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (!canCompileFile) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Auto-compile on save", &scriptEditorState.autoCompileOnSave);
    if (!canCompileFile) {
        ImGui::EndDisabled();
    }
    if (!hasFile) {
        ImGui::EndDisabled();
    }

    bool canReload = false;
    if (reloadCheckPath != scriptEditorState.filePath) {
        reloadCheckPath = scriptEditorState.filePath;
        cachedCanReload = false;
        lastReloadCheckTime = 0.0;
    }
    if (hasFile && scriptEditorState.hasWriteTime) {
        const double now = glfwGetTime();
        if (now - lastReloadCheckTime >= 0.25) {
            lastReloadCheckTime = now;
            cachedCanReload = false;
            std::error_code ec;
            if (fs::exists(scriptEditorState.filePath, ec)) {
                auto diskTime = fs::last_write_time(scriptEditorState.filePath, ec);
                if (!ec && diskTime > scriptEditorState.lastWriteTime) {
                    cachedCanReload = true;
                }
            }
        }
        canReload = cachedCanReload;
    }
    if (canReload) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "File changed on disk");
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            openScriptInEditor(scriptEditorState.filePath);
            cachedCanReload = false;
            lastReloadCheckTime = glfwGetTime();
        }
    }

    ImGui::Separator();

        if (ImGui::BeginTabBar("ScriptingTabs")) {
            if (ImGui::BeginTabItem("Editor")) {
                if (hasFile) {
                    if (!scriptTextEditorReady) {
                        initializeScriptEditor(scriptTextEditor);
                        scriptTextEditorReady = true;
                    }

                    ScriptEditorLanguage language = detectScriptEditorLanguage(scriptEditorState.filePath);
                    uint64_t bufferHash = hashBuffer(scriptEditorState.buffer);
                    bool fileChanged = (identifiersFilePath != scriptEditorState.filePath);
                    bool languageChanged = (activeLanguage != language);
                    if (bufferHash != identifiersHash || fileChanged || languageChanged) {
                        identifiersHash = bufferHash;
                        identifiersFilePath = scriptEditorState.filePath;
                        activeLanguage = language;

                        const auto& keywords = keywordsForLanguage(language);
                        bufferIdentifiers = extractIdentifiers(scriptEditorState.buffer, keywords);
                        bufferFunctions = extractFunctionIdentifiers(scriptEditorState.buffer, keywords);
                        bufferDefines = extractDefineIdentifiers(scriptEditorState.buffer);
                        scriptTextEditor.SetLanguageDefinition(buildLanguageDefinition(language, bufferFunctions, bufferDefines));
                        completionPoolDirty = true;
                    }

                    auto rebuildCompletionPool = [&]() -> bool {
                        if (!completionPoolDirty) return false;
                        completionPool.clear();
                        std::unordered_set<std::string> poolSet;
                        const auto& langDef = scriptTextEditor.GetLanguageDefinition();
                        for (const auto& kw : langDef.mKeywords) {
                            poolSet.insert(kw);
                        }
                        for (const auto& identifier : langDef.mIdentifiers) {
                            poolSet.insert(identifier.first);
                        }
                        for (const auto& identifier : langDef.mPreprocIdentifiers) {
                            poolSet.insert(identifier.first);
                        }
                        for (const auto& entry : scriptingCompletions) {
                            poolSet.insert(entry);
                        }
                        for (const auto& entry : symbols) {
                            poolSet.insert(entry);
                        }
                        for (const auto& entry : bufferIdentifiers) {
                            poolSet.insert(entry);
                        }
                        for (const auto& entry : bufferFunctions) {
                            poolSet.insert(entry);
                        }
                        for (const auto& entry : bufferDefines) {
                            poolSet.insert(entry);
                        }
                        completionPool.assign(poolSet.begin(), poolSet.end());
                        std::sort(completionPool.begin(), completionPool.end());
                        completionPoolDirty = false;
                        return true;
                    };

                    auto extractPrefixAtCursor = [&]() {
                        TextEditor::Coordinates cursor = scriptTextEditor.GetCursorPosition();
                        std::string prefix = scriptTextEditor.GetWordAtPublic(cursor);
                        if (prefix.empty() && cursor.mColumn > 0) {
                            TextEditor::Coordinates prev(cursor.mLine, cursor.mColumn - 1);
                            prefix = scriptTextEditor.GetWordAtPublic(prev);
                        }
                        return prefix;
                    };

                    auto updateSuggestions = [&](const std::string& prefix) {
                        activePrefix = prefix;
                        if (!activePrefix.empty() && activePrefix.size() >= 2) {
                            activeSuggestions = buildCompletionList(completionPool, activePrefix);
                        } else {
                            activeSuggestions.clear();
                        }
                    };

                    bool poolRebuiltBeforeRender = rebuildCompletionPool();
                    std::string prefixBeforeRender = extractPrefixAtCursor();
                    if (poolRebuiltBeforeRender || prefixBeforeRender != activePrefix) {
                        updateSuggestions(prefixBeforeRender);
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
                        completionPoolDirty = true;
                    }

                    bool poolRebuiltAfterRender = rebuildCompletionPool();
                    std::string prefixAfterRender = extractPrefixAtCursor();
                    if (poolRebuiltAfterRender || prefixAfterRender != activePrefix) {
                        updateSuggestions(prefixAfterRender);
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
                    ImGui::TextDisabled("Select a script or shader file to start editing.");
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
