#pragma once

#include "Common.h"
#include "ScriptCompiler.h"

#include <unordered_map>
#include <unordered_set>

enum class ScriptLanguageServiceLanguage {
    Cpp,
    C,
    GLSL,
    HLSL,
    Lua,
    ModuCPP
};

struct ScriptLanguageServiceProjectData {
    std::vector<fs::path> files;
    std::vector<std::string> projectSymbols;
};

struct ScriptLanguageServiceDocumentData {
    ScriptLanguageServiceLanguage language = ScriptLanguageServiceLanguage::Cpp;
    std::vector<std::string> identifiers;
    std::vector<std::string> functions;
    std::vector<std::string> defines;
    std::vector<std::string> symbols;
    std::unordered_map<std::string, std::string> functionSignatures;
};

struct ScriptLanguageServiceFunctionCallContext {
    bool valid = false;
    std::string functionName;
    int activeParameter = 0;
};

class ScriptLanguageService {
public:
    static ScriptLanguageServiceLanguage detectLanguage(const fs::path& path);
    static bool isCompilableScriptPath(const fs::path& path);

    static ScriptLanguageServiceProjectData scanProjectFiles(const fs::path& projectRoot,
                                                             const fs::path& assetsPath,
                                                             const ScriptBuildConfig* config);

    static ScriptLanguageServiceDocumentData analyzeDocument(const fs::path& path,
                                                             const std::string& text);

    static std::vector<std::string> buildCompletionList(const std::vector<std::string>& pool,
                                                        const std::string& prefix,
                                                        size_t limit = 48);

    static std::vector<std::string> splitSignatureParameters(const std::string& signature);

    static ScriptLanguageServiceFunctionCallContext detectFunctionCallContext(const std::string& currentLine,
                                                                              int cursorColumn);

    static const std::unordered_set<std::string>& keywordsForLanguage(ScriptLanguageServiceLanguage language);
};
