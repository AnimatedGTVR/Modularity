#include "Engine.h"

#include <set>

namespace {
enum class ModupakManagerView {
    All = 0,
    Project,
    Global,
    Updates,
    Incompatible
};

enum class ModupakManagerSort {
    NameAsc = 0,
    NameDesc,
    VersionDesc,
    Status
};

struct PackageUiState {
    bool inProject = false;
    bool global = false;
    bool installed = false;
    bool compatible = true;
    bool updateAvailable = false;
    bool availableLocally = false;
    bool availableRemotely = false;
};

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsSearchTerm(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return toLowerCopy(haystack).find(needle) != std::string::npos;
}

bool packageMatchesSearch(const PackageInfo& pkg, const std::string& loweredSearch) {
    if (loweredSearch.empty()) {
        return true;
    }
    return containsSearchTerm(pkg.name, loweredSearch) ||
           containsSearchTerm(pkg.id, loweredSearch) ||
           containsSearchTerm(pkg.subsystem, loweredSearch) ||
           containsSearchTerm(pkg.packageType, loweredSearch) ||
           containsSearchTerm(pkg.author, loweredSearch);
}

int versionWeight(const std::string& version) {
    std::stringstream ss(version);
    std::string token;
    int factor = 1000000;
    int value = 0;
    while (std::getline(ss, token, '.') && factor > 0) {
        try {
            value += std::stoi(token) * factor;
        } catch (...) {
        }
        factor /= 100;
    }
    return value;
}

const char* viewLabel(ModupakManagerView view) {
    switch (view) {
        case ModupakManagerView::All: return "All Packages";
        case ModupakManagerView::Project: return "In Project";
        case ModupakManagerView::Global: return "Global";
        case ModupakManagerView::Updates: return "Updates";
        case ModupakManagerView::Incompatible: return "Incompatible";
    }
    return "All Packages";
}

const char* sortLabel(ModupakManagerSort sortMode) {
    switch (sortMode) {
        case ModupakManagerSort::NameAsc: return "Name (A-Z)";
        case ModupakManagerSort::NameDesc: return "Name (Z-A)";
        case ModupakManagerSort::VersionDesc: return "Version (Newest)";
        case ModupakManagerSort::Status: return "Status";
    }
    return "Name (A-Z)";
}

PackageUiState buildPackageUiState(PackageManager& packageManager, const PackageInfo& pkg) {
    PackageUiState state;
    state.inProject = packageManager.isInstalled(pkg.id);
    state.global = packageManager.isGloballyInstalled(pkg.id);
    state.installed = state.inProject || state.global;
    state.compatible = packageManager.isCompatible(pkg);
    state.updateAvailable = packageManager.hasUpdateAvailable(pkg.id);
    state.availableLocally = !pkg.registrySourcePath.empty();
    state.availableRemotely = !pkg.downloadUrl.empty();
    return state;
}

bool matchesView(ModupakManagerView view, const PackageUiState& state) {
    switch (view) {
        case ModupakManagerView::All:
            return true;
        case ModupakManagerView::Project:
            return state.inProject;
        case ModupakManagerView::Global:
            return state.global;
        case ModupakManagerView::Updates:
            return state.updateAvailable;
        case ModupakManagerView::Incompatible:
            return !state.compatible;
    }
    return true;
}

bool drawSidebarItem(const char* label, int count, bool selected) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 32.0f;

    ImGui::InvisibleButton((std::string(label) + "##sidebar_item").c_str(), ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ImVec4 bg = ImVec4(0.12f, 0.14f, 0.18f, 0.0f);
    if (selected) {
        bg = ImVec4(0.20f, 0.29f, 0.43f, 0.92f);
        drawList->AddRectFilled(ImVec2(rect.Min.x, rect.Min.y + 4.0f),
                                ImVec2(rect.Min.x + 3.0f, rect.Max.y - 4.0f),
                                ImGui::GetColorU32(ImVec4(0.43f, 0.75f, 0.98f, 1.0f)),
                                2.0f);
    } else if (hovered) {
        bg = ImVec4(0.17f, 0.20f, 0.26f, 0.88f);
    }

    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(bg), 6.0f);

    const ImVec2 textPos(rect.Min.x + 12.0f, rect.Min.y + 7.0f);
    const std::string countText = std::to_string(count);
    const ImVec2 countSize = ImGui::CalcTextSize(countText.c_str());
    const ImVec2 countPos(rect.Max.x - 12.0f - countSize.x, rect.Min.y + 7.0f);

    drawList->AddText(textPos,
                      ImGui::GetColorU32(selected ? ImVec4(0.97f, 0.98f, 1.0f, 1.0f)
                                                  : ImVec4(0.77f, 0.81f, 0.89f, 1.0f)),
                      label);
    drawList->AddText(countPos,
                      ImGui::GetColorU32(selected ? ImVec4(0.97f, 0.98f, 1.0f, 0.98f)
                                                  : ImVec4(0.60f, 0.66f, 0.75f, 1.0f)),
                      countText.c_str());
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    return clicked;
}

std::string installedStateLabel(const PackageUiState& state) {
    if (state.inProject && state.global) {
        return "Installed in Project and Global";
    }
    if (state.inProject) {
        return "Installed in Project";
    }
    if (state.global) {
        return "Installed Globally";
    }
    return "Not Installed";
}

std::string sourceSummary(const PackageUiState& state) {
    if (state.availableLocally && state.availableRemotely) {
        return "Local registry + online archive";
    }
    if (state.availableLocally) {
        return "Local registry checkout";
    }
    if (state.availableRemotely) {
        return "Online registry download";
    }
    return "Package source unavailable";
}

float drawBadge(ImDrawList* drawList,
                ImVec2 pos,
                const std::string& label,
                const ImVec4& bgColor,
                const ImVec4& textColor = ImVec4(0.96f, 0.97f, 0.99f, 1.0f)) {
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 padding(7.0f, 3.0f);
    const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
    drawList->AddRectFilled(pos,
                            ImVec2(pos.x + size.x, pos.y + size.y),
                            ImGui::GetColorU32(bgColor),
                            5.0f);
    drawList->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y - 1.0f),
                      ImGui::GetColorU32(textColor),
                      label.c_str());
    return size.x;
}

void detailRow(const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(160.0f);
    if (value.empty()) {
        ImGui::TextUnformatted("-");
    } else {
        ImGui::TextWrapped("%s", value.c_str());
    }
}

void sectionHeader(const char* label) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", label);
}

bool drawPackageRow(const PackageInfo& pkg,
                    const PackageUiState& state,
                    bool selected) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 70.0f;
    ImGui::InvisibleButton((pkg.id + "##row").c_str(), ImVec2(width, height));

    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ImVec4 bg = ImVec4(0.13f, 0.15f, 0.20f, 0.95f);
    if (selected) {
        bg = ImVec4(0.20f, 0.31f, 0.48f, 1.0f);
    } else if (hovered) {
        bg = ImVec4(0.17f, 0.21f, 0.28f, 1.0f);
    }
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(bg), 8.0f);
    drawList->AddRect(rect.Min,
                      rect.Max,
                      ImGui::GetColorU32(selected ? ImVec4(0.40f, 0.67f, 0.96f, 0.80f)
                                                  : ImVec4(0.26f, 0.30f, 0.37f, 0.55f)),
                      8.0f,
                      0,
                      selected ? 1.4f : 1.0f);

    const ImVec2 pad(12.0f, 9.0f);
    const ImVec2 titlePos(rect.Min.x + pad.x, rect.Min.y + pad.y);
    const ImVec2 metaPos(rect.Min.x + pad.x, rect.Min.y + pad.y + 23.0f);
    const ImVec2 badgePos(rect.Min.x + pad.x, rect.Min.y + height - 24.0f);

    const std::string title = pkg.name.empty() ? pkg.id : pkg.name;
    drawList->AddText(titlePos, ImGui::GetColorU32(ImVec4(0.96f, 0.97f, 0.99f, 1.0f)), title.c_str());

    const std::string version = pkg.version.empty() ? "" : ("v" + pkg.version);
    const ImVec2 versionSize = ImGui::CalcTextSize(version.c_str());
    drawList->AddText(ImVec2(rect.Max.x - pad.x - versionSize.x, titlePos.y),
                      ImGui::GetColorU32(ImVec4(0.74f, 0.79f, 0.88f, 1.0f)),
                      version.c_str());

    std::string meta = pkg.id;
    if (!pkg.packageType.empty()) {
        meta += "  |  " + pkg.packageType;
    }
    if (!pkg.subsystem.empty()) {
        meta += "  |  " + pkg.subsystem;
    }
    drawList->AddText(metaPos,
                      ImGui::GetColorU32(ImVec4(0.63f, 0.68f, 0.76f, 1.0f)),
                      meta.c_str());

    float badgeX = badgePos.x;
    if (state.inProject) {
        badgeX += drawBadge(drawList, ImVec2(badgeX, badgePos.y), "Project", ImVec4(0.26f, 0.51f, 0.83f, 1.0f)) + 6.0f;
    }
    if (state.global) {
        badgeX += drawBadge(drawList, ImVec2(badgeX, badgePos.y), "Global", ImVec4(0.79f, 0.55f, 0.18f, 1.0f)) + 6.0f;
    }
    if (state.updateAvailable) {
        badgeX += drawBadge(drawList, ImVec2(badgeX, badgePos.y), "Update", ImVec4(0.24f, 0.62f, 0.42f, 1.0f)) + 6.0f;
    }
    if (!state.compatible) {
        badgeX += drawBadge(drawList, ImVec2(badgeX, badgePos.y), "Incompatible", ImVec4(0.78f, 0.28f, 0.24f, 1.0f)) + 6.0f;
    } else if (!state.installed) {
        badgeX += drawBadge(drawList, ImVec2(badgeX, badgePos.y), "Available", ImVec4(0.34f, 0.38f, 0.47f, 1.0f)) + 6.0f;
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    return clicked;
}
} // namespace

void Engine::renderRegistryPackagesWindow() {
    if (!showRegistryPackagesWindow) {
        return;
    }

    const bool wasOpen = showRegistryPackagesWindow;
    if (!ImGui::Begin("Modupak Manager", &showRegistryPackagesWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (wasOpen != showRegistryPackagesWindow && projectManager.currentProject.isLoaded) {
            saveEditorUserSettings();
        }
        return;
    }

    const auto& registry = packageManager.getRegistry();
    std::vector<const PackageInfo*> allPackages;
    std::set<std::string> subsystemSet;
    std::set<std::string> typeSet;
    allPackages.reserve(registry.size());

    std::array<int, 5> sidebarCounts = {0, 0, 0, 0, 0};
    for (const auto& pkg : registry) {
        if (!pkg.registryPackage) {
            continue;
        }
        allPackages.push_back(&pkg);
        const PackageUiState state = buildPackageUiState(packageManager, pkg);
        sidebarCounts[static_cast<int>(ModupakManagerView::All)]++;
        if (state.inProject) sidebarCounts[static_cast<int>(ModupakManagerView::Project)]++;
        if (state.global) sidebarCounts[static_cast<int>(ModupakManagerView::Global)]++;
        if (state.updateAvailable) sidebarCounts[static_cast<int>(ModupakManagerView::Updates)]++;
        if (!state.compatible) sidebarCounts[static_cast<int>(ModupakManagerView::Incompatible)]++;
        if (!pkg.subsystem.empty()) subsystemSet.insert(pkg.subsystem);
        if (!pkg.packageType.empty()) typeSet.insert(pkg.packageType);
    }

    std::vector<std::string> subsystemOptions = {"All Subsystems"};
    subsystemOptions.insert(subsystemOptions.end(), subsystemSet.begin(), subsystemSet.end());
    std::vector<std::string> typeOptions = {"All Types"};
    typeOptions.insert(typeOptions.end(), typeSet.begin(), typeSet.end());

    registryPackageView = std::clamp(registryPackageView, 0, 4);
    registryPackageSubsystemFilter = std::clamp(registryPackageSubsystemFilter, 0, static_cast<int>(subsystemOptions.size()) - 1);
    registryPackageTypeFilter = std::clamp(registryPackageTypeFilter, 0, static_cast<int>(typeOptions.size()) - 1);
    registryPackageSort = std::clamp(registryPackageSort, 0, 3);

    ImGui::BeginChild("ModupakToolbar", ImVec2(0.0f, 74.0f), true);
    ImGui::Text("Modupak Manager");
    ImGui::TextDisabled("%s", packageManager.getRegistryStatus().c_str());

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##ModupakSearch",
                             "Search packages",
                             registryPackageSearch,
                             sizeof(registryPackageSearch));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo("##SubsystemFilter", subsystemOptions[registryPackageSubsystemFilter].c_str())) {
        for (int i = 0; i < static_cast<int>(subsystemOptions.size()); ++i) {
            const bool selected = (registryPackageSubsystemFilter == i);
            if (ImGui::Selectable(subsystemOptions[i].c_str(), selected)) {
                registryPackageSubsystemFilter = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo("##TypeFilter", typeOptions[registryPackageTypeFilter].c_str())) {
        for (int i = 0; i < static_cast<int>(typeOptions.size()); ++i) {
            const bool selected = (registryPackageTypeFilter == i);
            if (ImGui::Selectable(typeOptions[i].c_str(), selected)) {
                registryPackageTypeFilter = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const ModupakManagerSort sortMode = static_cast<ModupakManagerSort>(registryPackageSort);
    if (ImGui::BeginCombo("##SortPackages", sortLabel(sortMode))) {
        for (int i = 0; i < 4; ++i) {
            const auto candidate = static_cast<ModupakManagerSort>(i);
            const bool selected = (registryPackageSort == i);
            if (ImGui::Selectable(sortLabel(candidate), selected)) {
                registryPackageSort = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        const std::string previousSelection = registryPackageSelectedId;
        packageManager.refreshRegistry();
        registryPackageFeedback = packageManager.getRegistryStatus();
        registryPackageLastActionSucceeded = packageManager.hasRegistryMetadata();
        registryPackageSelectedId = previousSelection;
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    ImGui::EndChild();

    const std::string loweredSearch = toLowerCopy(std::string(registryPackageSearch));
    std::vector<const PackageInfo*> filteredPackages;
    filteredPackages.reserve(allPackages.size());
    for (const PackageInfo* pkg : allPackages) {
        const PackageUiState state = buildPackageUiState(packageManager, *pkg);
        if (!matchesView(static_cast<ModupakManagerView>(registryPackageView), state)) {
            continue;
        }
        if (!packageMatchesSearch(*pkg, loweredSearch)) {
            continue;
        }
        if (registryPackageSubsystemFilter > 0 &&
            pkg->subsystem != subsystemOptions[registryPackageSubsystemFilter]) {
            continue;
        }
        if (registryPackageTypeFilter > 0 &&
            pkg->packageType != typeOptions[registryPackageTypeFilter]) {
            continue;
        }
        filteredPackages.push_back(pkg);
    }

    std::sort(filteredPackages.begin(), filteredPackages.end(),
        [&](const PackageInfo* lhs, const PackageInfo* rhs) {
            const PackageUiState leftState = buildPackageUiState(packageManager, *lhs);
            const PackageUiState rightState = buildPackageUiState(packageManager, *rhs);
            switch (static_cast<ModupakManagerSort>(registryPackageSort)) {
                case ModupakManagerSort::NameDesc:
                    if (lhs->name == rhs->name) return lhs->id > rhs->id;
                    return lhs->name > rhs->name;
                case ModupakManagerSort::VersionDesc:
                    if (versionWeight(lhs->version) == versionWeight(rhs->version)) {
                        return lhs->name < rhs->name;
                    }
                    return versionWeight(lhs->version) > versionWeight(rhs->version);
                case ModupakManagerSort::Status: {
                    const int leftRank = (leftState.updateAvailable ? 0 : 1) +
                                         (leftState.installed ? 0 : 3) +
                                         (leftState.compatible ? 0 : 10);
                    const int rightRank = (rightState.updateAvailable ? 0 : 1) +
                                          (rightState.installed ? 0 : 3) +
                                          (rightState.compatible ? 0 : 10);
                    if (leftRank == rightRank) return lhs->name < rhs->name;
                    return leftRank < rightRank;
                }
                case ModupakManagerSort::NameAsc:
                default:
                    if (lhs->name == rhs->name) return lhs->id < rhs->id;
                    return lhs->name < rhs->name;
            }
        });

    auto packageInFilteredList = [&](const std::string& id) {
        return std::any_of(filteredPackages.begin(), filteredPackages.end(), [&](const PackageInfo* pkg) {
            return pkg->id == id;
        });
    };

    if (registryPackageSelectedId.empty() || !packageInFilteredList(registryPackageSelectedId)) {
        registryPackageSelectedId = filteredPackages.empty() ? std::string() : filteredPackages.front()->id;
    }

    const PackageInfo* selectedPackage = nullptr;
    for (const PackageInfo* pkg : filteredPackages) {
        if (pkg->id == registryPackageSelectedId) {
            selectedPackage = pkg;
            break;
        }
    }
    if (!selectedPackage) {
        for (const PackageInfo* pkg : allPackages) {
            if (pkg->id == registryPackageSelectedId) {
                selectedPackage = pkg;
                break;
            }
        }
    }

    const float sidebarWidth = 170.0f;
    const float listWidth = std::max(280.0f, ImGui::GetContentRegionAvail().x * 0.40f);

    ImGui::Spacing();
    ImGui::BeginChild("ModupakSidebar", ImVec2(sidebarWidth, 0.0f), true);
    for (int i = 0; i < 5; ++i) {
        const auto view = static_cast<ModupakManagerView>(i);
        if (drawSidebarItem(viewLabel(view), sidebarCounts[i], registryPackageView == i)) {
            registryPackageView = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ModupakListPane", ImVec2(listWidth, 0.0f), true);
    ImGui::Text("Packages");
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", static_cast<int>(filteredPackages.size()));
    ImGui::Separator();
    ImGui::BeginChild("ModupakListScroll", ImVec2(0.0f, 0.0f), false);
    for (const PackageInfo* pkg : filteredPackages) {
        const PackageUiState state = buildPackageUiState(packageManager, *pkg);
        if (drawPackageRow(*pkg, state, registryPackageSelectedId == pkg->id)) {
            registryPackageSelectedId = pkg->id;
            selectedPackage = pkg;
        }
    }
    if (filteredPackages.empty()) {
        ImGui::TextDisabled("No packages matched the current view and filters.");
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ModupakDetailsPane", ImVec2(0.0f, 0.0f), true);
    if (!selectedPackage) {
        ImGui::TextDisabled("Select a package to view details.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    const PackageUiState selectedState = buildPackageUiState(packageManager, *selectedPackage);
    ImGui::BeginChild("ModupakDetailsScroll", ImVec2(0.0f, -88.0f), false);

    ImGui::Text("%s", selectedPackage->name.empty() ? selectedPackage->id.c_str() : selectedPackage->name.c_str());
    if (!selectedPackage->version.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("v%s", selectedPackage->version.c_str());
    }

    ImDrawList* detailDrawList = ImGui::GetWindowDrawList();
    ImVec2 badgePos = ImGui::GetCursorScreenPos();
    float badgeX = badgePos.x;
    if (selectedState.inProject) {
        badgeX += drawBadge(detailDrawList, ImVec2(badgeX, badgePos.y), "Installed", ImVec4(0.24f, 0.56f, 0.90f, 1.0f)) + 6.0f;
        badgeX += drawBadge(detailDrawList, ImVec2(badgeX, badgePos.y), "Project", ImVec4(0.26f, 0.51f, 0.83f, 1.0f)) + 6.0f;
    }
    if (selectedState.global) {
        badgeX += drawBadge(detailDrawList, ImVec2(badgeX, badgePos.y), "Global", ImVec4(0.79f, 0.55f, 0.18f, 1.0f)) + 6.0f;
    }
    if (selectedState.updateAvailable) {
        badgeX += drawBadge(detailDrawList, ImVec2(badgeX, badgePos.y), "Update Available", ImVec4(0.24f, 0.62f, 0.42f, 1.0f)) + 6.0f;
    }
    if (!selectedState.compatible) {
        badgeX += drawBadge(detailDrawList, ImVec2(badgeX, badgePos.y), "Incompatible", ImVec4(0.78f, 0.28f, 0.24f, 1.0f)) + 6.0f;
    }
    ImGui::Dummy(ImVec2(0.0f, 28.0f));

    sectionHeader("Overview");
    detailRow("Description", selectedPackage->description);

    sectionHeader("Metadata");
    detailRow("Package ID", selectedPackage->id);
    detailRow("Author", selectedPackage->author);
    detailRow("Version", selectedPackage->version);
    detailRow("Package Type", selectedPackage->packageType);
    detailRow("Subsystem", selectedPackage->subsystem);
    detailRow("Registry Source", sourceSummary(selectedState));

    sectionHeader("Installation");
    detailRow("Installed State", installedStateLabel(selectedState));
    detailRow("Project Install", selectedState.inProject ? "Enabled for this project" : "Not enabled for this project");
    detailRow("Global Install", selectedState.global ? "Present in global package store" : "Not present in global package store");
    if (!selectedPackage->downloadUrl.empty()) {
        detailRow("Download URL", selectedPackage->downloadUrl);
    }

    sectionHeader("Compatibility");
    detailRow("Compatible Engine", selectedPackage->compatibleModuEngineVersion);
    if (!selectedState.compatible) {
        ImGui::TextColored(ImVec4(0.92f, 0.47f, 0.38f, 1.0f),
                           "This package is unavailable because it is not compatible with the current ModuEngine version.");
    } else if (!selectedState.availableLocally && !selectedState.availableRemotely && !selectedState.global) {
        ImGui::TextColored(ImVec4(0.92f, 0.47f, 0.38f, 1.0f),
                           "Package content is missing from the local and online registry sources.");
    } else {
        ImGui::TextDisabled("This package can be installed with the current engine version.");
    }

    sectionHeader("Status");
    if (!packageManager.getRegistryLastUpdated().empty()) {
        detailRow("Registry Updated", packageManager.getRegistryLastUpdated());
    }
    if (!packageManager.getRegistryUpdatedBy().empty()) {
        detailRow("Updated By", packageManager.getRegistryUpdatedBy());
    }
    if (!registryPackageFeedback.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(registryPackageLastActionSucceeded
                               ? ImVec4(0.43f, 0.85f, 0.60f, 1.0f)
                               : ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                           "%s",
                           registryPackageFeedback.c_str());
    }

    ImGui::EndChild();

    const bool packageAvailable = selectedState.availableLocally || selectedState.availableRemotely || selectedState.global;
    const bool canInstallProject = projectManager.currentProject.isLoaded &&
                                   selectedState.compatible &&
                                   packageAvailable &&
                                   !selectedState.inProject;
    const bool canInstallGlobal = selectedState.compatible &&
                                  (selectedState.availableLocally || selectedState.availableRemotely) &&
                                  !selectedState.global;
    const bool canRemoveProject = selectedState.inProject;
    const bool canRemoveGlobal = selectedState.global;

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("Load a project to install packages into a project manifest.");
    }

    const float detailsWidth = ImGui::GetContentRegionAvail().x;
    const float buttonGap = 8.0f;
    const float actionBlockWidth = std::min(detailsWidth, 340.0f);
    const float projectButtonWidth = std::floor((actionBlockWidth - buttonGap) * 0.56f);
    const float secondaryButtonWidth = actionBlockWidth - buttonGap - projectButtonWidth;
    ImGui::SetCursorPosX(std::max(0.0f, detailsWidth - actionBlockWidth));

    ImGui::BeginDisabled(!canInstallProject);
    if (ImGui::Button("Install to Project", ImVec2(projectButtonWidth, 0.0f))) {
        if (packageManager.installRegistryPackageToProject(selectedPackage->id)) {
            clampOptionalPackageState(false);
            registryPackageFeedback = "Installed " + selectedPackage->id + " into the current project.";
            registryPackageLastActionSucceeded = true;
            if (audio.isReady()) {
                audio.playPreview("Resources/Sounds/Modupak Success installed.mp3", 0.95f, false);
            }
            addConsoleMessage(registryPackageFeedback, ConsoleMessageType::Success);
        } else {
            registryPackageFeedback = packageManager.getLastError();
            registryPackageLastActionSucceeded = false;
            addConsoleMessage("Package install failed: " + registryPackageFeedback, ConsoleMessageType::Error);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!canInstallGlobal);
    if (ImGui::Button("Install Globally", ImVec2(secondaryButtonWidth, 0.0f))) {
        if (packageManager.installRegistryPackageGlobally(selectedPackage->id)) {
            registryPackageFeedback = "Installed " + selectedPackage->id + " into the global package store.";
            registryPackageLastActionSucceeded = true;
            if (audio.isReady()) {
                audio.playPreview("Resources/Sounds/Modupak Success installed.mp3", 0.95f, false);
            }
            addConsoleMessage(registryPackageFeedback, ConsoleMessageType::Success);
        } else {
            registryPackageFeedback = packageManager.getLastError();
            registryPackageLastActionSucceeded = false;
            addConsoleMessage("Global package install failed: " + registryPackageFeedback, ConsoleMessageType::Error);
        }
    }
    ImGui::EndDisabled();

    ImGui::SetCursorPosX(std::max(0.0f, detailsWidth - actionBlockWidth));
    ImGui::BeginDisabled(!canRemoveProject);
    if (ImGui::Button("Remove from Project", ImVec2(projectButtonWidth, 0.0f))) {
        if (packageManager.remove(selectedPackage->id)) {
            clampOptionalPackageState(false);
            registryPackageFeedback = "Removed " + selectedPackage->id + " from the current project.";
            registryPackageLastActionSucceeded = true;
            addConsoleMessage(registryPackageFeedback, ConsoleMessageType::Success);
        } else {
            registryPackageFeedback = packageManager.getLastError();
            registryPackageLastActionSucceeded = false;
            addConsoleMessage("Project package removal failed: " + registryPackageFeedback, ConsoleMessageType::Error);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!canRemoveGlobal);
    if (ImGui::Button("Remove Globally", ImVec2(secondaryButtonWidth, 0.0f))) {
        if (packageManager.removeRegistryPackageGlobally(selectedPackage->id)) {
            registryPackageFeedback = "Removed " + selectedPackage->id + " from the global package store.";
            registryPackageLastActionSucceeded = true;
            addConsoleMessage(registryPackageFeedback, ConsoleMessageType::Success);
        } else {
            registryPackageFeedback = packageManager.getLastError();
            registryPackageLastActionSucceeded = false;
            addConsoleMessage("Global package removal failed: " + registryPackageFeedback, ConsoleMessageType::Error);
        }
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::End();
    if (wasOpen != showRegistryPackagesWindow && projectManager.currentProject.isLoaded) {
        saveEditorUserSettings();
    }
}
