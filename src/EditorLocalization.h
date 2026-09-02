#pragma once

#include <string>
#include <vector>

// Editor UI localization.
//
// Every visible editor string is addressed by a stable internal key
// ("WINDOW_INSPECTOR", "COMPONENT_RIGIDBODY3D_MASS"). The key never changes
// with the selected language and is never derived from the English text, so a
// reworded English label does not silently orphan every translation.
//
//   Resources/Languages/<Folder>/Editor.json   editor UI strings (this file)
//   Resources/Languages/<Folder>/ModuCPP.json  ModuCPP syntax dictionary
//                                              (see ModuCPPLanguagePack.h)
//
// Adding a language is a new folder. No recompile, no editor-window changes.
//
// Lookup order for every key:
//   1. the selected editor language
//   2. English (the shipped file, plus the built-in defaults compiled into the
//      call sites, so a missing or broken file can never blank the editor)
//   3. the key itself, with a one-shot warning logged
//
// This is display text only. Serialization keys, component ids, class names,
// file paths, asset ids, project data keys and ModuCPP identifiers are never
// touched by any of this.
//
// The editor language is a *global* preference (launcher_settings.modu). It is
// deliberately independent of the per-project ModuCPP syntax language, so
// "German editor, English scripts" and the reverse both work.
namespace Modularity {
namespace Loc {

struct LanguageInfo {
    std::string id;          // "english", "german" (lowercased folder name)
    std::string folderName;  // "English", "German"
    std::string displayName; // "German (Deutsch)"
    std::string endonym;     // "Deutsch"
    bool isCanonical = false;// English
    bool loadedFromDisk = false;
};

// Scans Resources/Languages/. Safe to call more than once; cheap after the first.
void Initialize();
// Re-reads every Editor.json without restarting the editor.
void ReloadFromDisk();

const std::vector<LanguageInfo>& Languages();
const LanguageInfo* FindLanguage(const std::string& id);
const LanguageInfo& CurrentLanguage();
const std::string& CurrentLanguageId();
std::string NormalizeLanguageId(const std::string& id);
int LanguageIndex(const std::string& id);

// Returns true when the active language actually changed.
bool SetLanguage(const std::string& id);
// Bumped on every language change. UI code that caches composed strings can
// compare against this to know when to rebuild.
unsigned Generation();

// Translated display text. `englishDefault` is the built-in English string and
// is registered into the English table on first use.
const char* T(const char* key, const char* englishDefault);

// Window/popup title carrying a stable ImGui identity:
//   Window("WINDOW_INSPECTOR", "Inspector") -> "Inspektor###Inspector"
// The id half stays the historical English name so docking layouts, focus
// calls and popup ids keep working across a language switch.
const char* Window(const char* key, const char* englishName);
// The matching id-only form for DockBuilderDockWindow / SetWindowFocus /
// OpenPopup / IsPopupOpen: WindowRef("Inspector") -> "###Inspector".
const char* WindowRef(const char* englishName);

// Labelled widget whose label doubles as its ImGui id (Checkbox, Combo,
// DragFloat, ...). Returns "<localized>###<key>", so the id is pinned to the
// stable key and does not move when the language does.
const char* Widget(const char* key, const char* englishLabel);

// Inspector field label. The serialized field name is never renamed; this only
// resolves what is drawn next to the widget. Falls back to English, then to the
// existing inspector formatting of the serialized name.
//   Field("COMPONENT_RIGIDBODY3D", "mass", "Mass")
const char* Field(const char* componentKey, const char* fieldName, const char* englishDefault);

// The editor's long-standing "camelCase -> Camel Case" inspector formatting.
std::string PrettyFieldName(const std::string& serializedName);

// Supported language id matching the OS locale, or "" when none matches.
// Never applied automatically; the editor asks first.
std::string DetectOperatingSystemLanguage();

// Keys that resolved to nothing in any language this session, for the
// Language Manager's diagnostics list.
const std::vector<std::string>& MissingKeys();

} // namespace Loc
} // namespace Modularity
