#include "Engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <unordered_map>

#if defined(_WIN32)
#include <shellapi.h>
#endif

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
        return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
               ext == ".moducpp" || ext == ".cs" || ext == ".csproj";
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

    static bool openPathInDefaultEditor(const fs::path& path) {
#ifdef _WIN32
        std::wstring widePath = path.wstring();
        HINSTANCE result = ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
#elif __linux__
        std::string cmd = "xdg-open \"" + path.string() + "\"";
        return std::system(cmd.c_str()) == 0;
#else
        (void)path;
        return false;
#endif
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
                                                        size_t limit = 48) {
        std::vector<std::string> matches;
        if (prefix.empty()) return matches;
        std::unordered_set<std::string> seen;
        for (const auto& entry : pool) {
            if (entry.rfind(prefix, 0) == 0) {
                if (seen.insert(entry).second) {
                    matches.push_back(entry);
                }
                if (matches.size() >= limit) break;
            }
        }
        if (matches.size() >= limit) return matches;
        const std::string lowerPrefix = toLowerCopy(prefix);
        for (const auto& entry : pool) {
            if (toLowerCopy(entry).rfind(lowerPrefix, 0) == 0) {
                if (seen.insert(entry).second) {
                    matches.push_back(entry);
                }
                if (matches.size() >= limit) break;
            }
        }
        return matches;
    }

    struct FunctionCallContext {
        bool valid = false;
        std::string functionName;
        int activeParameter = 0;
    };

    static std::vector<std::string> splitSignatureParameters(const std::string& signature) {
        std::vector<std::string> params;
        const size_t open = signature.find('(');
        const size_t close = signature.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
            return params;
        }
        std::string body = signature.substr(open + 1, close - open - 1);
        std::string current;
        int nested = 0;
        for (char c : body) {
            if (c == '<' || c == '(' || c == '[') {
                ++nested;
            } else if (c == '>' || c == ')' || c == ']') {
                nested = std::max(0, nested - 1);
            }
            if (c == ',' && nested == 0) {
                std::string trimmed = trimLeft(current);
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
                    trimmed.pop_back();
                }
                if (!trimmed.empty()) {
                    params.push_back(trimmed);
                }
                current.clear();
                continue;
            }
            current.push_back(c);
        }
        std::string trimmed = trimLeft(current);
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
            trimmed.pop_back();
        }
        if (!trimmed.empty()) {
            params.push_back(trimmed);
        }
        return params;
    }

    static std::unordered_map<std::string, std::string> extractFunctionSignatures(
        const std::string& text,
        const std::unordered_set<std::string>& keywords) {
        static const std::unordered_set<std::string> kSkip = {
            "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "defined", "layout"
        };

        std::unordered_map<std::string, std::string> signatures;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            std::string trimmed = trimLeft(line);
            if (trimmed.empty()) continue;
            if (trimmed.rfind("//", 0) == 0) continue;
            size_t openParen = trimmed.find('(');
            size_t closeParen = trimmed.find(')', openParen == std::string::npos ? 0 : openParen + 1);
            if (openParen == std::string::npos || closeParen == std::string::npos) continue;
            size_t nameEnd = openParen;
            while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(trimmed[nameEnd - 1]))) {
                --nameEnd;
            }
            size_t nameStart = nameEnd;
            while (nameStart > 0) {
                unsigned char c = static_cast<unsigned char>(trimmed[nameStart - 1]);
                if (isIdentifierBody(c) || trimmed[nameStart - 1] == ':' || trimmed[nameStart - 1] == '~') {
                    --nameStart;
                    continue;
                }
                break;
            }
            if (nameStart >= nameEnd) continue;
            std::string name = trimmed.substr(nameStart, nameEnd - nameStart);
            if (keywords.find(name) != keywords.end() || kSkip.find(name) != kSkip.end()) continue;
            size_t sigEnd = trimmed.find('{');
            if (sigEnd == std::string::npos) {
                sigEnd = trimmed.find(';');
            }
            std::string signature = (sigEnd == std::string::npos) ? trimmed : trimmed.substr(0, sigEnd);
            while (!signature.empty() && std::isspace(static_cast<unsigned char>(signature.back()))) {
                signature.pop_back();
            }
            if (signature.empty()) continue;
            signatures.emplace(name, signature);
        }
        return signatures;
    }

    static FunctionCallContext detectFunctionCallContext(const std::string& currentLine, int cursorColumn) {
        FunctionCallContext ctx;
        if (currentLine.empty()) return ctx;
        const int clampedColumn = std::clamp(cursorColumn, 0, static_cast<int>(currentLine.size()));
        int depth = 0;
        for (int i = clampedColumn - 1; i >= 0; --i) {
            char c = currentLine[static_cast<size_t>(i)];
            if (c == ')') {
                ++depth;
                continue;
            }
            if (c == '(') {
                if (depth > 0) {
                    --depth;
                    continue;
                }
                int end = i - 1;
                while (end >= 0 && std::isspace(static_cast<unsigned char>(currentLine[static_cast<size_t>(end)]))) {
                    --end;
                }
                int start = end;
                while (start >= 0) {
                    unsigned char ch = static_cast<unsigned char>(currentLine[static_cast<size_t>(start)]);
                    char raw = currentLine[static_cast<size_t>(start)];
                    if (isIdentifierBody(ch) || raw == ':' || raw == '~') {
                        --start;
                        continue;
                    }
                    break;
                }
                if (end <= start) return ctx;
                ctx.functionName = currentLine.substr(static_cast<size_t>(start + 1),
                    static_cast<size_t>(end - start));
                if (ctx.functionName.empty()) return ctx;
                int nested = 0;
                int argumentIndex = 0;
                for (int k = i + 1; k < clampedColumn; ++k) {
                    char argChar = currentLine[static_cast<size_t>(k)];
                    if (argChar == '(') {
                        ++nested;
                    } else if (argChar == ')') {
                        nested = std::max(0, nested - 1);
                    } else if (argChar == ',' && nested == 0) {
                        ++argumentIndex;
                    }
                }
                ctx.valid = true;
                ctx.activeParameter = std::max(0, argumentIndex);
                return ctx;
            }
            if ((c == ';' || c == '{' || c == '}') && depth == 0) {
                break;
            }
        }
        return ctx;
    }

    static ImVec2 popupPositionWithinBounds(const ImVec2& anchor,
                                            float popupWidth,
                                            float popupHeight,
                                            float lineHeight,
                                            const ImVec2& boundsMin,
                                            const ImVec2& boundsMax,
                                            bool* outFlippedUp = nullptr) {
        bool flipUp = false;
        float yBelow = anchor.y + lineHeight + 2.0f;
        float yAbove = anchor.y - popupHeight - 2.0f;
        if (yBelow + popupHeight > boundsMax.y && yAbove >= boundsMin.y) {
            flipUp = true;
        }
        float y = flipUp ? yAbove : yBelow;
        y = std::clamp(y, boundsMin.y, std::max(boundsMin.y, boundsMax.y - popupHeight));
        float x = std::clamp(anchor.x, boundsMin.x, std::max(boundsMin.x, boundsMax.x - popupWidth));
        if (outFlippedUp) {
            *outFlippedUp = flipUp;
        }
        return ImVec2(x, y);
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
        ".cpp", ".cc", ".cxx", ".c", ".moducpp", ".hpp", ".h", ".inl",
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

    if (!hasScriptingWindowPackage()) {
        if (!openPathInDefaultEditor(normalized)) {
            addConsoleMessage("Failed to open script in the system editor: " + normalized.string(),
                              ConsoleMessageType::Error);
        }
        return;
    }

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
    if (!hasScriptingWindowPackage()) {
        showScriptingWindow = false;
        return;
    }

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

    ImGuiIO& io = ImGui::GetIO();
    static std::vector<std::string> symbols;
    static uint64_t symbolsHash = 0;
    static std::vector<std::string> bufferIdentifiers;
    static std::vector<std::string> bufferFunctions;
    static std::vector<std::string> bufferDefines;
    static std::unordered_map<std::string, std::string> functionSignatures;
    static uint64_t identifiersHash = 0;
    static fs::path identifiersFilePath;
    static ScriptEditorLanguage activeLanguage = ScriptEditorLanguage::Cpp;
    static std::vector<std::string> completionPool;
    static std::vector<std::string> activeSuggestions;
    static std::string activePrefix;
    static bool completionPoolDirty = true;
    static int selectedSuggestionIndex = 0;
    static bool completionManuallyDismissed = false;
    static std::string completionDismissPrefix;
    static bool completionPopupVisibleLastFrame = false;
    static fs::path reloadCheckPath;
    static bool cachedCanReload = false;
    static double lastReloadCheckTime = 0.0;
    static std::string cachedFilterLower;
    static std::vector<int> filteredScriptIndices;
    static bool showFilePane = true;
    static bool showDetailsPane = true;
    static bool showDiagnosticsPane = true;
    static float filePaneWidth = 240.0f;
    static float detailsPaneWidth = 250.0f;
    static float diagnosticsPaneHeight = 160.0f;

    if (listRefreshedThisFrame) {
        completionPoolDirty = true;
    }

    ImGui::TextDisabled("Script & Shader Editor");
    ImGui::SameLine();
    if (ImGui::Button("Refresh List")) {
        scriptingFilesDirty = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Files", &showFilePane);
    ImGui::SameLine();
    ImGui::Checkbox("Details", &showDetailsPane);
    ImGui::SameLine();
    ImGui::Checkbox("Diagnostics", &showDiagnosticsPane);

    ImGui::Separator();

    const float topSplitThickness = 4.0f;
    if (showFilePane) {
        float availWidth = ImGui::GetContentRegionAvail().x;
        const float minFilePane = 180.0f;
        const float minMainPane = 380.0f;
        const float maxFilePane = std::max(minFilePane, availWidth - minMainPane - topSplitThickness);
        filePaneWidth = std::clamp(filePaneWidth, minFilePane, maxFilePane);

        ImGui::BeginChild("ScriptingFiles", ImVec2(filePaneWidth, 0.0f), true);
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
        float splitterHeight = ImGui::GetContentRegionAvail().y;
        if (splitterHeight < 1.0f) splitterHeight = 1.0f;
        ImGui::InvisibleButton("ScriptingFilePaneSplitter", ImVec2(topSplitThickness, splitterHeight));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            filePaneWidth += io.MouseDelta.x;
            filePaneWidth = std::clamp(filePaneWidth, minFilePane, maxFilePane);
        }
        ImGui::SameLine();
    }

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
                    functionSignatures = extractFunctionSignatures(scriptEditorState.buffer, keywords);
                    if (language == ScriptEditorLanguage::Cpp || language == ScriptEditorLanguage::C) {
                        functionSignatures["Begin"] = "void Begin()";
                        functionSignatures["TickUpdate"] = "void TickUpdate(float deltaTime)";
                        functionSignatures["Spec"] = "void Spec()";
                        functionSignatures["TestEditor"] = "void TestEditor()";
                        functionSignatures["Update"] = "void Update(float deltaTime)";
                    }
                    scriptTextEditor.SetLanguageDefinition(buildLanguageDefinition(language, bufferFunctions, bufferDefines));
                    completionPoolDirty = true;
                }

                auto rebuildCompletionPool = [&]() -> bool {
                    if (!completionPoolDirty) return false;
                    completionPool.clear();
                    std::unordered_set<std::string> poolSet;
                    const auto& langDef = scriptTextEditor.GetLanguageDefinition();
                    for (const auto& kw : langDef.mKeywords) poolSet.insert(kw);
                    for (const auto& identifier : langDef.mIdentifiers) poolSet.insert(identifier.first);
                    for (const auto& identifier : langDef.mPreprocIdentifiers) poolSet.insert(identifier.first);
                    for (const auto& entry : scriptingCompletions) poolSet.insert(entry);
                    for (const auto& entry : symbols) poolSet.insert(entry);
                    for (const auto& entry : bufferIdentifiers) poolSet.insert(entry);
                    for (const auto& entry : bufferFunctions) poolSet.insert(entry);
                    for (const auto& entry : bufferDefines) poolSet.insert(entry);
                    completionPool.assign(poolSet.begin(), poolSet.end());
                    std::sort(completionPool.begin(), completionPool.end());
                    completionPoolDirty = false;
                    return true;
                };

                struct ActiveTokenInfo {
                    bool valid = false;
                    TextEditor::Coordinates cursor;
                    TextEditor::Coordinates start;
                    TextEditor::Coordinates end;
                    std::string word;
                    std::string prefix;
                };

                auto activeToken = [&]() -> ActiveTokenInfo {
                    ActiveTokenInfo token;
                    token.cursor = scriptTextEditor.GetCursorPosition();
                    TextEditor::Coordinates probe = token.cursor;
                    token.word = scriptTextEditor.GetWordAtPublic(probe);
                    if (token.word.empty() && token.cursor.mColumn > 0) {
                        probe = TextEditor::Coordinates(token.cursor.mLine, token.cursor.mColumn - 1);
                        token.word = scriptTextEditor.GetWordAtPublic(probe);
                    }
                    if (token.word.empty()) return token;
                    token.start = scriptTextEditor.FindWordStartPublic(probe);
                    token.end = scriptTextEditor.FindWordEndPublic(probe);
                    int prefixLength = std::clamp(token.cursor.mColumn - token.start.mColumn, 0, static_cast<int>(token.word.size()));
                    token.prefix = token.word.substr(0, static_cast<size_t>(prefixLength));
                    token.valid = true;
                    return token;
                };

                auto updateSuggestions = [&](const std::string& prefix) {
                    bool prefixChanged = (prefix != activePrefix);
                    activePrefix = prefix;
                    if (prefixChanged) {
                        selectedSuggestionIndex = 0;
                    }
                    if (!completionDismissPrefix.empty() && activePrefix != completionDismissPrefix) {
                        completionManuallyDismissed = false;
                    }
                    if (!activePrefix.empty()) {
                        activeSuggestions = buildCompletionList(completionPool, activePrefix);
                    } else {
                        activeSuggestions.clear();
                    }
                    if (activeSuggestions.empty()) {
                        selectedSuggestionIndex = 0;
                    } else {
                        selectedSuggestionIndex = std::clamp(selectedSuggestionIndex, 0, static_cast<int>(activeSuggestions.size()) - 1);
                    }
                };

                auto applySuggestion = [&](const std::string& suggestion) {
                    ActiveTokenInfo token = activeToken();
                    TextEditor::Coordinates cursor = scriptTextEditor.GetCursorPosition();
                    TextEditor::Coordinates replaceStart = token.valid
                        ? token.start
                        : TextEditor::Coordinates(cursor.mLine, std::max(0, cursor.mColumn - static_cast<int>(activePrefix.size())));
                    TextEditor::Coordinates replaceEnd = token.valid ? token.end : cursor;
                    scriptTextEditor.SetSelection(replaceStart, replaceEnd);
                    scriptTextEditor.Delete();
                    scriptTextEditor.InsertText(suggestion.c_str());
                    scriptEditorState.dirty = true;
                    scriptEditorState.buffer = scriptTextEditor.GetText();
                    completionPoolDirty = true;
                    completionManuallyDismissed = true;
                    completionDismissPrefix = suggestion;
                };

                bool poolRebuiltBeforeRender = rebuildCompletionPool();
                ActiveTokenInfo tokenBeforeRender = activeToken();
                if (poolRebuiltBeforeRender || tokenBeforeRender.prefix != activePrefix) {
                    updateSuggestions(tokenBeforeRender.prefix);
                }
                bool interceptCompletionKeys = !activeSuggestions.empty() && !activePrefix.empty() && !completionManuallyDismissed;
                scriptTextEditor.SetAllowTabInput(!interceptCompletionKeys);
                scriptTextEditor.SetAllowArrowNavigation(!interceptCompletionKeys);
                scriptTextEditor.SetAllowEnterInput(!interceptCompletionKeys);

                const float paneSplitter = 4.0f;
                const float minEditorHeight = 120.0f;
                float availHeight = ImGui::GetContentRegionAvail().y;
                if (showDiagnosticsPane) {
                    diagnosticsPaneHeight = std::clamp(diagnosticsPaneHeight, 100.0f, std::max(100.0f, availHeight - minEditorHeight - paneSplitter));
                }
                float editorRowHeight = showDiagnosticsPane
                    ? std::max(minEditorHeight, availHeight - diagnosticsPaneHeight - paneSplitter)
                    : availHeight;

                ImVec2 editorRectMin(0.0f, 0.0f);
                ImVec2 editorRectMax(0.0f, 0.0f);
                bool hasEditorRect = false;

                ImGui::BeginChild("ScriptEditorRow", ImVec2(0.0f, editorRowHeight), false);
                float rowAvailWidth = ImGui::GetContentRegionAvail().x;
                const float minEditorWidth = 220.0f;
                const float minDetailsWidth = 170.0f;
                if (showDetailsPane) {
                    float maxDetailsWidth = std::max(minDetailsWidth, rowAvailWidth - minEditorWidth - paneSplitter);
                    detailsPaneWidth = std::clamp(detailsPaneWidth, minDetailsWidth, maxDetailsWidth);
                }
                float editorPaneWidth = showDetailsPane
                    ? std::max(minEditorWidth, rowAvailWidth - detailsPaneWidth - paneSplitter)
                    : rowAvailWidth;

                ImGui::BeginChild("ScriptEditorPane", ImVec2(editorPaneWidth, 0.0f), true);
                scriptTextEditor.Render("##ScriptEditor", ImVec2(0.0f, 0.0f), false);
                editorRectMin = ImGui::GetItemRectMin();
                editorRectMax = ImGui::GetItemRectMax();
                hasEditorRect = true;
                if (scriptTextEditor.IsTextChanged()) {
                    scriptEditorState.dirty = true;
                    scriptEditorState.buffer = scriptTextEditor.GetText();
                }
                ImGui::EndChild();

                if (showDetailsPane) {
                    ImGui::SameLine();
                    float detailsSplitterHeight = ImGui::GetContentRegionAvail().y;
                    if (detailsSplitterHeight < 1.0f) detailsSplitterHeight = 1.0f;
                    ImGui::InvisibleButton("ScriptingDetailsSplitter", ImVec2(paneSplitter, detailsSplitterHeight));
                    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    }
                    if (ImGui::IsItemActive()) {
                        detailsPaneWidth -= io.MouseDelta.x;
                        float maxDetailsWidth = std::max(minDetailsWidth, rowAvailWidth - minEditorWidth - paneSplitter);
                        detailsPaneWidth = std::clamp(detailsPaneWidth, minDetailsWidth, maxDetailsWidth);
                    }
                    ImGui::SameLine();
                    ImGui::BeginChild("ScriptDetailsPane", ImVec2(0.0f, 0.0f), true);
                    ImGui::TextDisabled("Symbols");
                    if (!symbols.empty()) {
                        ImGuiListClipper symbolClipper;
                        symbolClipper.Begin(static_cast<int>(symbols.size()));
                        while (symbolClipper.Step()) {
                            for (int i = symbolClipper.DisplayStart; i < symbolClipper.DisplayEnd; ++i) {
                                ImGui::BulletText("%s", symbols[static_cast<size_t>(i)].c_str());
                            }
                        }
                    } else {
                        ImGui::TextDisabled("No symbols detected.");
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("IntelliSense");
                    ImGui::Text("Pool: %d", static_cast<int>(completionPool.size()));
                    ImGui::Text("Prefix: %s", activePrefix.empty() ? "-" : activePrefix.c_str());
                    ImGui::Text("Suggestions: %d", static_cast<int>(activeSuggestions.size()));
                    ImGui::EndChild();
                }
                ImGui::EndChild();

                if (showDiagnosticsPane) {
                    float splitterWidth = ImGui::GetContentRegionAvail().x;
                    if (splitterWidth < 1.0f) splitterWidth = 1.0f;
                    ImGui::InvisibleButton("ScriptingDiagnosticsSplitter", ImVec2(splitterWidth, paneSplitter));
                    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                    }
                    if (ImGui::IsItemActive()) {
                        diagnosticsPaneHeight -= io.MouseDelta.y;
                        diagnosticsPaneHeight = std::clamp(diagnosticsPaneHeight, 100.0f,
                            std::max(100.0f, availHeight - minEditorHeight - paneSplitter));
                    }

                    ImGui::BeginChild("ScriptDiagnosticsPane", ImVec2(0.0f, diagnosticsPaneHeight), true);
                    if (ImGui::BeginTabBar("ScriptDiagnosticsTabs")) {
                        if (ImGui::BeginTabItem("Diagnostics")) {
                            int errorCount = 0;
                            int warningCount = 0;
                            int infoCount = 0;
                            for (const auto& diagnostic : lastCompileDiagnostics) {
                                if (diagnostic.severity == Modularity::ScriptDiagnosticSeverity::Error) {
                                    ++errorCount;
                                } else if (diagnostic.severity == Modularity::ScriptDiagnosticSeverity::Warning) {
                                    ++warningCount;
                                } else {
                                    ++infoCount;
                                }
                            }

                            ImGui::Text("Status: %s", lastCompileStatus.empty() ? "Idle" : lastCompileStatus.c_str());
                            ImGui::Text("Errors: %d  Warnings: %d  Info: %d", errorCount, warningCount, infoCount);
                            ImGui::Separator();
                            if (lastCompileDiagnostics.empty()) {
                                ImGui::TextDisabled("No diagnostics.");
                            } else {
                                for (size_t index = 0; index < lastCompileDiagnostics.size(); ++index) {
                                    const auto& diagnostic = lastCompileDiagnostics[index];
                                    const ImVec4 color =
                                        diagnostic.severity == Modularity::ScriptDiagnosticSeverity::Error
                                            ? ImVec4(0.94f, 0.45f, 0.45f, 1.0f)
                                            : diagnostic.severity == Modularity::ScriptDiagnosticSeverity::Warning
                                                ? ImVec4(0.95f, 0.79f, 0.40f, 1.0f)
                                                : ImVec4(0.55f, 0.78f, 0.95f, 1.0f);

                                    ImGui::PushID(static_cast<int>(index));
                                    ImGui::TextColored(
                                        color,
                                        "[%s][%s] %s",
                                        Modularity::scriptDiagnosticSeverityToString(diagnostic.severity),
                                        diagnostic.code.c_str(),
                                        diagnostic.message.c_str());
                                    if (!diagnostic.hint.empty()) {
                                        ImGui::TextWrapped("Hint: %s", diagnostic.hint.c_str());
                                    }
                                    if (!diagnostic.source.empty()) {
                                        ImGui::TextDisabled("Source: %s", diagnostic.source.c_str());
                                    }
                                    if (diagnostic.line > 0) {
                                        ImGui::TextDisabled("Line: %d", diagnostic.line);
                                    }
                                    if (!diagnostic.rawDetails.empty() && ImGui::TreeNode("Raw Details")) {
                                        ImGui::TextDisabled("Origin: %s", Modularity::scriptDiagnosticOriginToString(diagnostic.origin));
                                        if (diagnostic.column > 0) {
                                            ImGui::TextDisabled("Column: %d", diagnostic.column);
                                        }
                                        if (!diagnostic.sourceLine.empty()) {
                                            ImGui::TextWrapped("Code: %s", diagnostic.sourceLine.c_str());
                                        }
                                        ImGui::Separator();
                                        ImGui::TextWrapped("%s", diagnostic.rawDetails.c_str());
                                        ImGui::TreePop();
                                    }
                                    if (index + 1 < lastCompileDiagnostics.size()) {
                                        ImGui::Separator();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Output")) {
                            if (lastCompileLog.empty()) {
                                ImGui::TextDisabled("No build output.");
                            } else {
                                ImGui::BeginChild("ScriptOutputLog", ImVec2(0.0f, 0.0f), false,
                                    ImGuiWindowFlags_HorizontalScrollbar);
                                ImGui::TextUnformatted(lastCompileLog.c_str());
                                ImGui::EndChild();
                            }
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                    ImGui::EndChild();
                }

                uint64_t newHash = hashBuffer(scriptEditorState.buffer);
                if (newHash != symbolsHash) {
                    symbolsHash = newHash;
                    symbols = buildSymbolList(scriptEditorState.buffer);
                    completionPoolDirty = true;
                }
                bool poolRebuiltAfterRender = rebuildCompletionPool();
                ActiveTokenInfo tokenAfterRender = activeToken();
                if (poolRebuiltAfterRender || tokenAfterRender.prefix != activePrefix) {
                    updateSuggestions(tokenAfterRender.prefix);
                }

                bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                if (editorFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                    completionManuallyDismissed = false;
                    completionDismissPrefix.clear();
                }

                bool completionVisible = editorFocused &&
                    hasEditorRect &&
                    scriptTextEditor.HasCursorScreenPosition() &&
                    !activeSuggestions.empty() &&
                    !activePrefix.empty() &&
                    !completionManuallyDismissed;

                if (completionVisible) {
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
                        if (selectedSuggestionIndex <= 0) {
                            selectedSuggestionIndex = static_cast<int>(activeSuggestions.size()) - 1;
                        } else {
                            --selectedSuggestionIndex;
                        }
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
                        selectedSuggestionIndex = (selectedSuggestionIndex + 1) % static_cast<int>(activeSuggestions.size());
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                        completionManuallyDismissed = true;
                        completionDismissPrefix = activePrefix;
                        completionVisible = false;
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
                        const std::string accepted = activeSuggestions[static_cast<size_t>(selectedSuggestionIndex)];
                        applySuggestion(accepted);
                        completionVisible = false;
                    }
                }

                bool completionFlippedUp = false;
                ImVec2 caretPos = scriptTextEditor.GetCursorScreenPositionPublic();
                float lineHeight = ImGui::GetTextLineHeightWithSpacing();
                if (completionVisible) {
                    bool popupJustOpened = !completionPopupVisibleLastFrame;
                    const int visibleRows = std::min(10, static_cast<int>(activeSuggestions.size()));
                    const float popupWidth = 340.0f;
                    const float popupHeight = lineHeight * (visibleRows + 1.9f);
                    ImVec2 popupPos = popupPositionWithinBounds(caretPos, popupWidth, popupHeight, lineHeight,
                        editorRectMin, editorRectMax, &completionFlippedUp);
                    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
                    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
                    ImGuiWindowFlags popupFlags =
                        ImGuiWindowFlags_NoDocking |
                        ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoFocusOnAppearing;
                    if (ImGui::Begin("##ScriptCompletionPopup", nullptr, popupFlags)) {
                        ImGui::TextDisabled("Autocomplete");
                        ImGui::Separator();
                        ImGui::BeginChild("##ScriptCompletionList", ImVec2(0.0f, 0.0f), false,
                            ImGuiWindowFlags_AlwaysVerticalScrollbar);
                        for (size_t i = 0; i < activeSuggestions.size(); ++i) {
                            bool selected = (static_cast<int>(i) == selectedSuggestionIndex);
                            if (ImGui::Selectable(activeSuggestions[i].c_str(), selected)) {
                                selectedSuggestionIndex = static_cast<int>(i);
                                applySuggestion(activeSuggestions[i]);
                                completionVisible = false;
                                break;
                            }
                            if (selected && popupJustOpened) {
                                ImGui::SetScrollHereY(0.5f);
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::End();
                }
                completionPopupVisibleLastFrame = completionVisible;

                if (editorFocused && hasEditorRect && scriptTextEditor.HasCursorScreenPosition()) {
                    std::string signatureLabel;
                    FunctionCallContext callCtx = detectFunctionCallContext(
                        scriptTextEditor.GetCurrentLineText(),
                        scriptTextEditor.GetCursorPosition().mColumn);
                    if (callCtx.valid) {
                        auto tryFindSignature = [&](const std::string& name) -> std::string {
                            auto it = functionSignatures.find(name);
                            if (it != functionSignatures.end()) return it->second;
                            size_t scope = name.rfind("::");
                            if (scope != std::string::npos && scope + 2 < name.size()) {
                                std::string shortName = name.substr(scope + 2);
                                it = functionSignatures.find(shortName);
                                if (it != functionSignatures.end()) return it->second;
                            }
                            return {};
                        };
                        signatureLabel = tryFindSignature(callCtx.functionName);
                        if (signatureLabel.empty()) {
                            signatureLabel = callCtx.functionName + "(...)";
                        }

                        std::vector<std::string> params = splitSignatureParameters(signatureLabel);
                        const float sigWidth = 420.0f;
                        const float sigHeight = params.empty()
                            ? lineHeight * 2.2f
                            : lineHeight * std::clamp(static_cast<float>(params.size() + 2), 3.0f, 8.0f);

                        ImVec2 sigPos;
                        if (completionVisible && !completionFlippedUp) {
                            sigPos = ImVec2(caretPos.x, caretPos.y - sigHeight - 6.0f);
                            sigPos.x = std::clamp(sigPos.x, editorRectMin.x, std::max(editorRectMin.x, editorRectMax.x - sigWidth));
                            sigPos.y = std::clamp(sigPos.y, editorRectMin.y, std::max(editorRectMin.y, editorRectMax.y - sigHeight));
                        } else {
                            sigPos = popupPositionWithinBounds(caretPos, sigWidth, sigHeight, lineHeight,
                                editorRectMin, editorRectMax, nullptr);
                        }

                        ImGui::SetNextWindowPos(sigPos, ImGuiCond_Always);
                        ImGui::SetNextWindowSize(ImVec2(sigWidth, sigHeight), ImGuiCond_Always);
                        ImGuiWindowFlags sigFlags =
                            ImGuiWindowFlags_NoDocking |
                            ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoFocusOnAppearing;
                        if (ImGui::Begin("##ScriptSignaturePopup", nullptr, sigFlags)) {
                            ImGui::TextUnformatted(signatureLabel.c_str());
                            if (!params.empty()) {
                                ImGui::Separator();
                                const int highlighted = std::clamp(callCtx.activeParameter, 0, static_cast<int>(params.size()) - 1);
                                for (size_t i = 0; i < params.size(); ++i) {
                                    bool isActive = static_cast<int>(i) == highlighted;
                                    ImVec4 color = isActive
                                        ? ImVec4(0.96f, 0.84f, 0.40f, 1.0f)
                                        : ImVec4(0.75f, 0.75f, 0.78f, 1.0f);
                                    ImGui::TextColored(color, "%s%s", isActive ? "> " : "  ", params[i].c_str());
                                }
                            }
                        }
                        ImGui::End();
                    }
                }

                if (completionVisible && scriptTextEditor.HasCursorScreenPosition() && !activeSuggestions.empty()) {
                    const std::string& suggestion = activeSuggestions[static_cast<size_t>(selectedSuggestionIndex)];
                    if (suggestion.size() > activePrefix.size() && suggestion.rfind(activePrefix, 0) == 0) {
                        std::string ghost = suggestion.substr(activePrefix.size());
                        ImU32 ghostColor = IM_COL32(180, 180, 180, 110);
                        ImGui::GetForegroundDrawList()->AddText(scriptTextEditor.GetCursorScreenPositionPublic(),
                            ghostColor, ghost.c_str());
                    }
                }
            } else {
                scriptTextEditor.SetAllowTabInput(true);
                scriptTextEditor.SetAllowArrowNavigation(true);
                scriptTextEditor.SetAllowEnterInput(true);
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
