#include "ScriptLanguageService.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {
    static std::string toLowerCopy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static std::string trimLeft(const std::string& value) {
        size_t start = value.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        return value.substr(start);
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

    static std::vector<std::string> extractIdentifiers(const std::string& text,
                                                       const std::unordered_set<std::string>& keywords) {
        std::unordered_set<std::string> unique;
        std::string token;
        token.reserve(64);
        auto flushToken = [&]() {
            if (token.size() >= 2 && keywords.find(token) != keywords.end()) {
                token.clear();
                return;
            }
            if (token.size() >= 2) {
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

const std::unordered_set<std::string>& ScriptLanguageService::keywordsForLanguage(
    ScriptLanguageServiceLanguage language) {
    static const std::unordered_set<std::string> kCppKeywords = {
        "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr", "continue",
        "default", "delete", "do", "double", "else", "enum", "explicit", "extern", "false", "float",
        "for", "friend", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "operator", "private", "protected", "public", "return", "short", "signed", "sizeof", "static",
        "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typename",
        "union", "unsigned", "using", "virtual", "void", "volatile", "while"
    };
    static const std::unordered_set<std::string> kCKeywords = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
        "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
        "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
        "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
    };
    static const std::unordered_set<std::string> kGlslKeywords = {
        "attribute", "const", "uniform", "varying", "layout", "centroid", "flat", "smooth", "noperspective",
        "break", "continue", "do", "for", "while", "switch", "case", "default", "if", "else", "in", "out",
        "inout", "float", "double", "int", "void", "bool", "true", "false", "invariant", "discard", "return",
        "mat2", "mat3", "mat4", "dmat2", "dmat3", "dmat4", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
        "bvec2", "bvec3", "bvec4", "sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler2DShadow",
        "samplerCubeShadow", "sampler2DArray", "sampler2DArrayShadow", "isampler2D", "usampler2D", "struct",
        "precision", "highp", "mediump", "lowp", "uint"
    };
    static const std::unordered_set<std::string> kLuaKeywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
        "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
    };

    switch (language) {
        case ScriptLanguageServiceLanguage::C:
            return kCKeywords;
        case ScriptLanguageServiceLanguage::GLSL:
        case ScriptLanguageServiceLanguage::HLSL:
            return kGlslKeywords;
        case ScriptLanguageServiceLanguage::Lua:
            return kLuaKeywords;
        case ScriptLanguageServiceLanguage::Cpp:
        default:
            return kCppKeywords;
    }
}

ScriptLanguageServiceLanguage ScriptLanguageService::detectLanguage(const fs::path& path) {
    const std::string ext = extensionLower(path);
    if (ext == ".c") return ScriptLanguageServiceLanguage::C;
    if (ext == ".glsl" || ext == ".vert" || ext == ".frag") return ScriptLanguageServiceLanguage::GLSL;
    if (ext == ".hlsl" || ext == ".shader") return ScriptLanguageServiceLanguage::HLSL;
    if (ext == ".lua") return ScriptLanguageServiceLanguage::Lua;
    if (ext == ".moducpp") return ScriptLanguageServiceLanguage::ModuCPP;
    return ScriptLanguageServiceLanguage::Cpp;
}

bool ScriptLanguageService::isCompilableScriptPath(const fs::path& path) {
    const std::string ext = extensionLower(path);
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
           ext == ".moducpp" || ext == ".cs" || ext == ".csproj";
}

ScriptLanguageServiceProjectData ScriptLanguageService::scanProjectFiles(const fs::path& projectRoot,
                                                                         const fs::path& assetsPath,
                                                                         const ScriptBuildConfig* config) {
    ScriptLanguageServiceProjectData result;

    static const std::unordered_set<std::string> kValidExt = {
        ".cpp", ".cc", ".cxx", ".c", ".moducpp", ".hpp", ".h", ".inl",
        ".glsl", ".vert", ".frag", ".hlsl", ".shader", ".lua"
    };

    std::vector<fs::path> roots;
    if (config != nullptr) {
        roots.push_back(config->scriptsDir);
    } else {
        roots.push_back(assetsPath / "Scripts");
    }
    roots.push_back(projectRoot / "Scripts");
    roots.push_back(projectRoot / "Assets" / "Scripts");
    roots.push_back(assetsPath / "Shaders");

    std::unordered_set<std::string> uniquePaths;
    for (const auto& root : roots) {
        std::error_code ec;
        fs::path resolvedRoot = root;
        if (!resolvedRoot.empty() && !resolvedRoot.is_absolute()) {
            resolvedRoot = projectRoot / resolvedRoot;
        }
        if (resolvedRoot.empty() || !fs::exists(resolvedRoot, ec) || !fs::is_directory(resolvedRoot, ec)) {
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(resolvedRoot, ec);
             it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_directory()) continue;
            const std::string ext = extensionLower(it->path());
            if (kValidExt.find(ext) == kValidExt.end()) continue;
            fs::path normalized = it->path().lexically_normal();
            std::string key = normalized.string();
            if (!uniquePaths.insert(key).second) continue;
            result.files.push_back(normalized);
        }
    }

    std::sort(result.files.begin(), result.files.end());

    std::unordered_set<std::string> uniqueSymbols;
    for (const auto& scriptPath : result.files) {
        std::ifstream file(scriptPath);
        if (!file.is_open()) continue;
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::vector<std::string> symbols = buildSymbolList(buffer.str());
        for (const auto& symbol : symbols) {
            uniqueSymbols.insert(symbol);
        }
    }

    result.projectSymbols.assign(uniqueSymbols.begin(), uniqueSymbols.end());
    std::sort(result.projectSymbols.begin(), result.projectSymbols.end());
    return result;
}

ScriptLanguageServiceDocumentData ScriptLanguageService::analyzeDocument(const fs::path& path,
                                                                        const std::string& text) {
    ScriptLanguageServiceDocumentData result;
    result.language = detectLanguage(path);
    const auto& keywords = keywordsForLanguage(result.language);
    result.identifiers = extractIdentifiers(text, keywords);
    result.functions = extractFunctionIdentifiers(text, keywords);
    result.defines = extractDefineIdentifiers(text);
    result.symbols = buildSymbolList(text);
    result.functionSignatures = extractFunctionSignatures(text, keywords);
    return result;
}

std::vector<std::string> ScriptLanguageService::buildCompletionList(const std::vector<std::string>& pool,
                                                                    const std::string& prefix,
                                                                    size_t limit) {
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

std::vector<std::string> ScriptLanguageService::splitSignatureParameters(const std::string& signature) {
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

ScriptLanguageServiceFunctionCallContext ScriptLanguageService::detectFunctionCallContext(const std::string& currentLine,
                                                                                          int cursorColumn) {
    ScriptLanguageServiceFunctionCallContext ctx;
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
