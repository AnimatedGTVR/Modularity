#pragma once

#include "Common.h"
#include <fstream>

inline void WriteModularityGitIgnore(const fs::path& projectPath) {
    const fs::path ignorePath = projectPath / ".gitignore";
    if (fs::exists(ignorePath)) return;

    std::ofstream out(ignorePath);
    out << "# Modularity generated data\n"
        << "Library/\n"
        << "ProjectUserSettings/\n"
        << "Build/\n"
        << "Builds/\n"
        << "*.modupak\n"
        << "*.log\n"
        << "CrashReports/\n"
        << "\n# Platform/editor files\n"
        << ".DS_Store\n"
        << "Thumbs.db\n"
        << ".idea/\n"
        << ".vscode/\n";
}
