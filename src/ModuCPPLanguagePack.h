#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

// ModuCPP can be written in more than one human language.
//
// A syntax language is a *dictionary*, never a dialect. The lexer maps the
// localized spellings back onto the canonical (English) ModuCPP tokens before
// anything else runs, so the parser, the transpiler, the C++ backend,
// IntelliSense and the formatter only ever see one language:
//
//     Language Dictionary -> Lexer -> Canonical Tokens -> Parser -> AST -> C++ Backend
//
// There is exactly one parser. Adding Japanese (or anything else) means adding
// one row to kLanguageTable in ModuCPPLanguagePack.cpp and nothing else: no new
// parser, no new backend, no branch anywhere in the transpiler.
//
// Only language keywords and officially localized built-in API aliases are
// translated. User classes, fields, methods, namespaces, assets and project
// identifiers are never renamed.
namespace ModuCPPLang {

// Where a localized spelling is allowed to match.
enum class AliasPosition {
    // Anywhere a token may start. Never matches right after '.', "::" or "->",
    // so a member called `obj.Wenn` is left alone.
    Keyword,
    // Only right after '.', "::" or "->". Used for the officially localized
    // built-in API aliases (e.g. German `timer.Starten` -> `timer.Start`).
    Member
};

struct KeywordAlias {
    std::string localized;
    std::string canonical;
    AliasPosition position = AliasPosition::Keyword;
    // Spellings that are also plausible English identifiers (German `Start`
    // maps to `Begin`, for example) must not vote when auto-detecting which
    // language a file was written in, or every English script would look
    // slightly German.
    bool identifying = true;
};

struct LanguagePack {
    std::string id;          // stable lowercase id, serialized into project.modu
    std::string displayName; // "German (Deutsch)"
    std::string endonym;     // "Deutsch"
    std::string folderName;  // Resources/Languages/<folderName>/ModuCPP.json
    bool isCanonical = false;
    bool loadedFromDisk = false;
    std::vector<KeywordAlias> aliases;
};

// Re-reads Resources/Languages/<Folder>/ModuCPP.json. A file replaces the
// built-in dictionary for its id, or registers a language the engine has never
// heard of, so adding a syntax language needs no recompile. The built-in table
// stays as the fallback when a file is missing or malformed.
void ReloadFromDisk();

// Registered languages, canonical (English) first.
const std::vector<LanguagePack>& Languages();
const LanguagePack& CanonicalLanguage();
const LanguagePack* FindLanguage(const std::string& id);
const LanguagePack& FindLanguageOrCanonical(const std::string& id);
// Index into Languages(), or 0 (canonical) when the id is unknown.
int LanguageIndex(const std::string& id);
std::string NormalizeLanguageId(const std::string& id);

struct Detection {
    std::string languageId; // canonical id when nothing else scored
    int score = 0;          // identifying keyword hits for the winning pack
};

// Which language a source file appears to be written in. Comments, string and
// character literals and preprocessor lines never vote.
Detection DetectLanguage(const std::string& source);

// Localized source -> canonical ModuCPP source. Line structure is preserved
// (tokens are substituted in place), so diagnostics keep their line numbers.
std::string Canonicalize(const std::string& source, const LanguagePack& pack);
// Detects the language first. Returns the source unchanged for English.
std::string CanonicalizeAuto(const std::string& source);
// Canonical ModuCPP source -> localized source.
std::string Localize(const std::string& canonicalSource, const LanguagePack& pack);
std::string Translate(const std::string& source, const LanguagePack& from, const LanguagePack& to);
std::string TranslateAuto(const std::string& source, const LanguagePack& to,
                          std::string* outDetectedLanguageId = nullptr);

// Every localized spelling from every registered pack. IntelliSense folds these
// into the ModuCPP keyword set so localized sources do not read as a wall of
// unknown identifiers.
const std::unordered_set<std::string>& LocalizedVocabulary();

struct ScriptLanguageSurvey {
    std::vector<std::filesystem::path> files;      // scripts not written in the target language
    std::vector<std::string> detectedLanguageIds;  // distinct, in registration order
};

// Walks a file or directory and reports every .moducpp script that is not
// already written in `target`.
ScriptLanguageSurvey SurveyScripts(const std::filesystem::path& root, const LanguagePack& target);

struct TranslationReport {
    int translated = 0;
    int unchanged = 0;
    int failed = 0;
    std::string firstError;
};

// Rewrites each script into `target` in place. Files that already match are
// left untouched on disk.
TranslationReport TranslateScriptFiles(const std::vector<std::filesystem::path>& files,
                                       const LanguagePack& target);

} // namespace ModuCPPLang
