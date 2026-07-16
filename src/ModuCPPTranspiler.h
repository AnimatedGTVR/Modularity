#pragma once
#include "Common.h"
struct ModuCPPTranspileResult {
    std::string generatedSource;
    std::string className;
    // true when the C backend handled this script: generatedSource is plain C
    // against ScriptRuntimeCAPI.h (write it as .gen.c, it compiles in
    // milliseconds). false means the classic C++ backend ran.
    bool generatedC = false;
    // when the C backend declined, the first unsupported construct it hit.
    // purely informational; the C++ backend output is always the safe answer.
    std::string cBackendNote;
};
class ModuCPPTranspiler {
public:
    static bool shouldTranspile(const fs::path& sourcePath, const std::string& sourceText);
    bool transpile(const fs::path& sourcePath, const std::string& sourceText, ModuCPPTranspileResult& outResult, std::string& error) const;
};

