#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <optional>
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

void Engine::renderHierarchyPanel() {
    ImGui::Begin("Hierarchy", &showHierarchy);

    static char searchBuffer[128] = "";
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 headerBg = style.Colors[ImGuiCol_MenuBarBg];
    headerBg.x = std::min(headerBg.x + 0.02f, 1.0f);
    headerBg.y = std::min(headerBg.y + 0.02f, 1.0f);
    headerBg.z = std::min(headerBg.z + 0.02f, 1.0f);
    ImVec4 listBg = style.Colors[ImGuiCol_WindowBg];
    listBg.x = std::min(listBg.x + 0.01f, 1.0f);
    listBg.y = std::min(listBg.y + 0.01f, 1.0f);
    listBg.z = std::min(listBg.z + 0.01f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, headerBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("HierarchyHeader", ImVec2(0, 50), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    std::string filter = searchBuffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            setParent(draggedId, -1);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, listBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
    ImGui::BeginChild("HierarchyList", ImVec2(0, 0), true);

    for (size_t i = 0; i < sceneObjects.size(); i++) {
        if (sceneObjects[i].parentId != -1)
            continue;

        renderObjectNode(sceneObjects[i], filter);
    }

    if (ImGui::BeginPopupContextWindow("HierarchyBackground",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create"))
        {
            // ── Primitives ─────────────────────────────
            if (ImGui::BeginMenu("Primitives"))
            {
                if (ImGui::MenuItem("Cube"))    addObject(ObjectType::Cube, "Cube");
                if (ImGui::MenuItem("Sphere"))  addObject(ObjectType::Sphere, "Sphere");
                if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
                if (ImGui::MenuItem("Mirror"))  addObject(ObjectType::Mirror, "Mirror");
                ImGui::EndMenu();
            }

            // ── Lights ────────────────────────────────
            if (ImGui::BeginMenu("Lights"))
            {
                if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
                if (ImGui::MenuItem("Point Light"))       addObject(ObjectType::PointLight, "Point Light");
                if (ImGui::MenuItem("Spot Light"))        addObject(ObjectType::SpotLight, "Spot Light");
                if (ImGui::MenuItem("Area Light"))        addObject(ObjectType::AreaLight, "Area Light");
                ImGui::EndMenu();
            }

            // ── Other / Effects ───────────────────────
            if (ImGui::BeginMenu("Effects"))
            {
                if (ImGui::MenuItem("Post FX Node")) addObject(ObjectType::PostFXNode, "Post FX");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");

            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::End();
}

void Engine::renderObjectNode(SceneObject& obj, const std::string& filter) {
    std::string nameLower = obj.name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    if (!filter.empty() && nameLower.find(filter) == std::string::npos) {
        return;
    }

    bool hasChildren = !obj.childIds.empty();
    bool isSelected = std::find(selectedObjectIds.begin(), selectedObjectIds.end(), obj.id) != selectedObjectIds.end();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    const char* icon = "";
    switch (obj.type) {
        case ObjectType::Cube: icon = "[#]"; break;
        case ObjectType::Sphere: icon = "(O)"; break;
        case ObjectType::Capsule: icon = "[|]"; break;
        case ObjectType::OBJMesh: icon = "[M]"; break;
        case ObjectType::Model: icon = "[A]"; break;
        case ObjectType::Camera: icon = "(C)"; break;
        case ObjectType::DirectionalLight: icon = "(D)"; break;
        case ObjectType::PointLight: icon = "(P)"; break;
        case ObjectType::SpotLight: icon = "(S)"; break;
        case ObjectType::AreaLight: icon = "(L)"; break;
        case ObjectType::PostFXNode: icon = "(FX)"; break;
        case ObjectType::Mirror: icon = "[R]"; break;
    }

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, flags, "%s %s", icon, obj.name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
        setPrimarySelection(obj.id, additive);
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("SCENE_OBJECT", &obj.id, sizeof(int));
        ImGui::Text("Moving: %s", obj.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            if (draggedId != obj.id) {
                setParent(draggedId, obj.id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) {
            setPrimarySelection(obj.id);
            duplicateSelected();
        }
        if (ImGui::MenuItem("Delete")) {
            setPrimarySelection(obj.id);
            deleteSelected();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Parent") && obj.parentId != -1) {
            setParent(obj.id, -1);
        }
        ImGui::EndPopup();
    }

    if (nodeOpen) {
        for (int childId : obj.childIds) {
            auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                [childId](const SceneObject& o) { return o.id == childId; });
            if (it != sceneObjects.end()) {
                renderObjectNode(*it, filter);
            }
        }
        ImGui::TreePop();
    }
}

void Engine::renderInspectorPanel() {
    ImGui::Begin("Inspector", &showInspector);

    fs::path selectedMaterialPath;
    bool browserHasMaterial = false;
    fs::path selectedAudioPath;
    bool browserHasAudio = false;
    const AudioClipPreview* selectedAudioPreview = nullptr;
    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
        fs::directory_entry entry(fileBrowser.selectedFile);
        FileCategory cat = fileBrowser.getFileCategory(entry);
        if (cat == FileCategory::Material) {
            selectedMaterialPath = entry.path();
            browserHasMaterial = true;
            if (inspectedMaterialPath != selectedMaterialPath.string()) {
                inspectedMaterialValid = loadMaterialData(
                    selectedMaterialPath.string(),
                    inspectedMaterial,
                    inspectedAlbedo,
                    inspectedOverlay,
                    inspectedNormal,
                    inspectedUseOverlay,
                    &inspectedVertShader,
                    &inspectedFragShader
                );
                inspectedMaterialPath = selectedMaterialPath.string();
            }
        } else {
            inspectedMaterialPath.clear();
            inspectedMaterialValid = false;
        }
        if (cat == FileCategory::Audio) {
            selectedAudioPath = entry.path();
            browserHasAudio = true;
            selectedAudioPreview = audio.getPreview(selectedAudioPath.string());
        }
    } else {
        inspectedMaterialPath.clear();
        inspectedMaterialValid = false;
    }

    auto drawWaveform = [&](const char* id, const AudioClipPreview* preview, const ImVec2& size, float progressRatio, float* seekRatioOut) {
        if (!preview || preview->waveform.empty()) {
            ImGui::Dummy(size);
            return;
        }
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImVec2 end = ImVec2(start.x + size.x, start.y + size.y);
        ImGui::InvisibleButton(id, size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(start, end, IM_COL32(30, 35, 45, 180), 4.0f);
        float midY = (start.y + end.y) * 0.5f;
        float usableHeight = size.y * 0.45f;
        size_t count = preview->waveform.size();
        float step = count > 1 ? size.x / static_cast<float>(count - 1) : size.x;
        ImU32 color = IM_COL32(255, 180, 100, 200);
        for (size_t i = 0; i < count; ++i) {
            float amp = std::clamp(preview->waveform[i], 0.0f, 1.0f);
            float x = start.x + step * static_cast<float>(i);
            float yOff = amp * usableHeight;
            dl->AddLine(ImVec2(x, midY - yOff), ImVec2(x, midY + yOff), color, 1.2f);
        }

        if (progressRatio >= 0.0f && progressRatio <= 1.0f) {
            float px = start.x + progressRatio * size.x;
            dl->AddLine(ImVec2(px, start.y), ImVec2(px, end.y), IM_COL32(120, 210, 255, 230), 2.0f);
        }

        if (seekRatioOut && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float ratio = (mouseX - start.x) / size.x;
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            *seekRatioOut = ratio;
        }
    };

    struct ComponentHeaderState {
        bool open = false;
        bool enabledChanged = false;
    };

    auto drawComponentHeader = [&](const char* label, const char* id, bool* enabled, bool defaultOpen,
                                   const std::function<void()>& menuFn) -> ComponentHeaderState {
        ComponentHeaderState state;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        std::string headerId = std::string(label) + "##" + id;
        ImGui::SetNextItemAllowOverlap();
        state.open = ImGui::CollapsingHeader(headerId.c_str(), flags);

        ImVec2 headerMin = ImGui::GetItemRectMin();
        ImVec2 headerMax = ImGui::GetItemRectMax();
        ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
        float headerHeight = headerMax.y - headerMin.y;
        float controlSize = ImGui::GetFrameHeight();
        ImGuiStyle& style = ImGui::GetStyle();
        float right = headerMax.x - style.FramePadding.x;

        ImGui::PushID(id);
        if (menuFn) {
            ImVec2 menuPos(right - controlSize, headerMin.y + (headerHeight - controlSize) * 0.5f);
            ImGui::SetCursorScreenPos(menuPos);
            if (ImGui::SmallButton("...")) {
                ImGui::OpenPopup("ComponentMenu");
            }
            if (ImGui::BeginPopup("ComponentMenu")) {
                menuFn();
                ImGui::EndPopup();
            }
            right = menuPos.x - style.ItemSpacing.x;
        }
        if (enabled) {
            ImVec2 checkPos(right - controlSize, headerMin.y + (headerHeight - controlSize) * 0.5f);
            ImGui::SetCursorScreenPos(checkPos);
            if (ImGui::Checkbox("##Enabled", enabled)) {
                state.enabledChanged = true;
            }
        }
        ImGui::PopID();

        ImGui::SetCursorScreenPos(cursorAfter);
        return state;
    };

    auto renderMaterialAssetPanel = [&](const char* headerTitle, bool allowApply) {
        if (!browserHasMaterial) return;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.35f, 0.55f, 1.0f));
        if (ImGui::CollapsingHeader(headerTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(8.0f);
            if (!inspectedMaterialValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to read material file.");
            } else {
                auto textureField = [&](const char* label, const char* idSuffix, std::string& path) {
                    bool changed = false;
                    ImGui::PushID(idSuffix);
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-140);
                    char buf[512] = {};
                    std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                    if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                        path = buf;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    ImGui::SameLine();
                    bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                     fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                    ImGui::BeginDisabled(!canUseTex);
                    std::string btnLabel = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btnLabel.c_str())) {
                        path = fileBrowser.selectedFile.string();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    return changed;
                };

                ImGui::TextDisabled("%s", selectedMaterialPath.filename().string().c_str());
                ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", selectedMaterialPath.string().c_str());
                ImGui::Spacing();

                bool matChanged = false;
                if (ImGui::ColorEdit3("Base Color", &inspectedMaterial.color.x)) {
                    matChanged = true;
                }
                float metallic = inspectedMaterial.specularStrength;
                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                    inspectedMaterial.specularStrength = metallic;
                    matChanged = true;
                }
                float smoothness = inspectedMaterial.shininess / 256.0f;
                if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
                    smoothness = std::clamp(smoothness, 0.0f, 1.0f);
                    inspectedMaterial.shininess = smoothness * 256.0f;
                    matChanged = true;
                }
                if (ImGui::SliderFloat("Ambient Light", &inspectedMaterial.ambientStrength, 0.0f, 1.0f)) {
                    matChanged = true;
                }
                if (ImGui::SliderFloat("Detail Mix", &inspectedMaterial.textureMix, 0.0f, 1.0f)) {
                    matChanged = true;
                }

                ImGui::Spacing();
                matChanged |= textureField("Base Map", "PreviewAlbedo", inspectedAlbedo);
                if (ImGui::Checkbox("Use Detail Map", &inspectedUseOverlay)) {
                    matChanged = true;
                }
                matChanged |= textureField("Detail Map", "PreviewOverlay", inspectedOverlay);
                matChanged |= textureField("Normal Map", "PreviewNormal", inspectedNormal);

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "Shader");
                auto shaderField = [&](const char* label, const char* idSuffix, std::string& path) {
                    bool changed = false;
                    ImGui::PushID(idSuffix);
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-140);
                    char buf[512] = {};
                    std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                    if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                        path = buf;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    bool selectionIsShader = false;
                    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                        selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!selectionIsShader);
                    std::string btn = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btn.c_str())) {
                        path = fileBrowser.selectedFile.string();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    return changed;
                };
                matChanged |= shaderField("Vertex Shader", "PreviewVert", inspectedVertShader);
                matChanged |= shaderField("Fragment Shader", "PreviewFrag", inspectedFragShader);

                ImGui::BeginDisabled(inspectedVertShader.empty() && inspectedFragShader.empty());
                if (ImGui::Button("Reload Shader")) {
                    renderer.forceReloadShader(inspectedVertShader, inspectedFragShader);
                }
                ImGui::EndDisabled();

                ImGui::Spacing();
                if (ImGui::Button("Reload")) {
                    inspectedMaterialValid = loadMaterialData(
                        selectedMaterialPath.string(),
                        inspectedMaterial,
                        inspectedAlbedo,
                        inspectedOverlay,
                        inspectedNormal,
                        inspectedUseOverlay,
                        &inspectedVertShader,
                        &inspectedFragShader
                    );
                }
                ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    if (saveMaterialData(
                            selectedMaterialPath.string(),
                            inspectedMaterial,
                            inspectedAlbedo,
                            inspectedOverlay,
                            inspectedNormal,
                            inspectedUseOverlay,
                            inspectedVertShader,
                            inspectedFragShader))
                    {
                        addConsoleMessage("Saved material: " + selectedMaterialPath.string(), ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to save material: " + selectedMaterialPath.string(), ConsoleMessageType::Error);
                    }
                }

                if (allowApply) {
                    ImGui::SameLine();
                    SceneObject* target = getSelectedObject();
                    bool canApply = target != nullptr;
                    ImGui::BeginDisabled(!canApply);
                    if (ImGui::Button("Apply to Selection")) {
                        if (target) {
                            target->material = inspectedMaterial;
                            target->albedoTexturePath = inspectedAlbedo;
                            target->overlayTexturePath = inspectedOverlay;
                            target->normalMapPath = inspectedNormal;
                            target->useOverlay = inspectedUseOverlay;
                            target->materialPath = selectedMaterialPath.string();
                            target->vertexShaderPath = inspectedVertShader;
                            target->fragmentShaderPath = inspectedFragShader;
                            projectManager.currentProject.hasUnsavedChanges = true;
                            addConsoleMessage("Applied material to " + target->name, ConsoleMessageType::Success);
                        }
                    }
                    ImGui::EndDisabled();
                }

                if (matChanged) {
                    inspectedMaterialValid = true;
                }
            }
            ImGui::Unindent(8.0f);
        }
        ImGui::PopStyleColor();
    };

    auto renderAudioAssetPanel = [&](const char* headerTitle, SceneObject* target) {
        if (!browserHasAudio) return;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.4f, 0.25f, 1.0f));
        if (ImGui::CollapsingHeader(headerTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(8.0f);
            ImGui::TextDisabled("%s", selectedAudioPath.filename().string().c_str());
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", selectedAudioPath.string().c_str());
            ImGui::Spacing();

            if (selectedAudioPreview) {
                double cur = 0.0;
                double dur = 0.0;
                float progress = -1.0f;
                if (audio.getPreviewTime(selectedAudioPath.string(), cur, dur) && dur > 0.0001) {
                    progress = static_cast<float>(cur / dur);
                }
                ImGui::Text("Format: %u ch @ %u Hz", selectedAudioPreview->channels, selectedAudioPreview->sampleRate);
                ImGui::Text("Length: %.2f s", selectedAudioPreview->durationSeconds);
                ImVec2 waveSize(ImGui::GetContentRegionAvail().x, 96.0f);
                float seekRatio = -1.0f;
                drawWaveform("##AudioWaveAsset", selectedAudioPreview, waveSize, progress, &seekRatio);
                if (seekRatio >= 0.0f && dur > 0.0) {
                    audio.seekPreview(selectedAudioPath.string(), seekRatio * dur);
                }
                if (dur > 0.0) {
                    ImGui::TextDisabled("Time: %0.2f / %0.2f", cur, dur);
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "Unable to decode audio preview.");
            }

            ImGui::Spacing();
            bool isPlayingPreview = audio.isPreviewing(selectedAudioPath.string());
            if (ImGui::Button(isPlayingPreview ? "Stop" : "Play", ImVec2(72, 0))) {
                if (isPlayingPreview) {
                    audio.stopPreview();
                } else {
                    audio.playPreview(selectedAudioPath.string());
                }
            }

            if (target) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Assign to Selection")) {
                    if (!target->hasAudioSource) {
                        target->hasAudioSource = true;
                        target->audioSource = AudioSourceComponent{};
                    }
                    target->audioSource.clipPath = selectedAudioPath.string();
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }

            ImGui::Unindent(8.0f);
        }
        ImGui::PopStyleColor();
    };

    if (selectedObjectIds.empty()) {
        if (browserHasMaterial) {
            renderMaterialAssetPanel("Material Asset", true);
        } else if (browserHasAudio) {
            renderAudioAssetPanel("Audio Clip", nullptr);
        } else {
            ImGui::TextDisabled("No object selected");
        }
        ImGui::End();
        return;
    }

    int primaryId = selectedObjectId;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [primaryId](const SceneObject& obj) { return obj.id == primaryId; });

    if (it == sceneObjects.end()) {
        ImGui::TextDisabled("Object not found");
        ImGui::End();
        return;
    }

    SceneObject& obj = *it;
    ImGui::PushID(obj.id); // Scope per-object widgets to avoid ID collisions

    if (selectedObjectIds.size() > 1) {
        ImGui::Text("Multiple objects selected: %zu", selectedObjectIds.size());
        ImGui::Separator();
    }

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));

    if (ImGui::CollapsingHeader("Object Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        char nameBuffer[128];
        strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
            obj.name = nameBuffer;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Text("Type:");
        ImGui::SameLine();
        const char* typeLabel = "Unknown";
        switch (obj.type) {
            case ObjectType::Cube:       typeLabel = "Cube"; break;
            case ObjectType::Sphere:     typeLabel = "Sphere"; break;
            case ObjectType::Capsule:    typeLabel = "Capsule"; break;
            case ObjectType::OBJMesh:    typeLabel = "OBJ Mesh"; break;
            case ObjectType::Model:      typeLabel = "Model"; break;
        case ObjectType::Camera: typeLabel = "Camera"; break;
        case ObjectType::DirectionalLight: typeLabel = "Directional Light"; break;
        case ObjectType::PointLight: typeLabel = "Point Light"; break;
        case ObjectType::SpotLight:  typeLabel = "Spot Light"; break;
        case ObjectType::AreaLight:  typeLabel = "Area Light"; break;
        case ObjectType::PostFXNode: typeLabel = "Post FX Node"; break;
        case ObjectType::Mirror:     typeLabel = "Mirror"; break;
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", typeLabel);

        ImGui::Text("ID:");
        ImGui::SameLine();
        ImGui::TextDisabled("%d", obj.id);

        if (ImGui::Checkbox("Enabled##ObjEnabled", &obj.enabled)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Text("Layer:");
        ImGui::SameLine();
        int layer = obj.layer;
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##Layer", &layer, 0, 31)) {
            obj.layer = layer;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(0-31)");

        ImGui::Text("Tag:");
        ImGui::SameLine();
        char tagBuf[64] = {};
        std::snprintf(tagBuf, sizeof(tagBuf), "%s", obj.tag.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Tag", tagBuf, sizeof(tagBuf))) {
            obj.tag = tagBuf;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }

    ImGui::PopStyleColor();

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.5f, 0.3f, 1.0f));

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("Transform");
        ImGui::Indent(10.0f);

        if (obj.type == ObjectType::PostFXNode) {
            ImGui::TextDisabled("Transform is ignored for post-processing nodes.");
        }

        ImGui::Text("Position");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Position", &obj.position.x, 0.1f)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Rotation");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Rotation", &obj.rotation.x, 1.0f, -360.0f, 360.0f)) {
            obj.rotation = NormalizeEulerDegrees(obj.rotation);
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Scale");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f, 0.01f, 100.0f)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
            obj.position = glm::vec3(0.0f);
            obj.rotation = glm::vec3(0.0f);
            obj.scale = glm::vec3(1.0f);
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Unindent(10.0f);
        ImGui::PopID();
    }

    ImGui::PopStyleColor();

    if (obj.hasCollider) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.5f, 0.35f, 1.0f));
        bool removeCollider = false;
        bool changed = false;
        auto header = drawComponentHeader("Collider", "Collider", &obj.collider.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeCollider = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Collider");
            ImGui::Indent(10.0f);

            const char* colliderTypes[] = { "Box", "Mesh", "Convex Mesh", "Capsule" };
            int colliderType = static_cast<int>(obj.collider.type);
            if (ImGui::Combo("Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes))) {
                obj.collider.type = static_cast<ColliderType>(colliderType);
                changed = true;
            }

            if (obj.collider.type == ColliderType::Box) {
                if (ImGui::DragFloat3("Box Size", &obj.collider.boxSize.x, 0.01f, 0.01f, 1000.0f, "%.3f")) {
                    obj.collider.boxSize.x = std::max(0.01f, obj.collider.boxSize.x);
                    obj.collider.boxSize.y = std::max(0.01f, obj.collider.boxSize.y);
                    obj.collider.boxSize.z = std::max(0.01f, obj.collider.boxSize.z);
                    changed = true;
                }
                if (ImGui::SmallButton("Match Object Scale")) {
                    obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                    changed = true;
                }
            } else if (obj.collider.type == ColliderType::Capsule) {
                float radius = std::max(0.05f, std::max(obj.collider.boxSize.x, obj.collider.boxSize.z) * 0.5f);
                float height = std::max(0.1f, obj.collider.boxSize.y);
                if (ImGui::DragFloat("Radius", &radius, 0.01f, 0.05f, 5.0f, "%.3f")) {
                    obj.collider.boxSize.x = obj.collider.boxSize.z = radius * 2.0f;
                    changed = true;
                }
                if (ImGui::DragFloat("Height", &height, 0.01f, 0.1f, 10.0f, "%.3f")) {
                    obj.collider.boxSize.y = height;
                    changed = true;
                }
                ImGui::TextDisabled("Capsule aligned to Y axis.");
            } else {
                if (ImGui::Checkbox("Use Convex Hull (required for Rigidbody)", &obj.collider.convex)) {
                    changed = true;
                }
                ImGui::TextDisabled("Uses mesh from the object (OBJ/Model). Non-convex is static-only.");
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeCollider) {
            obj.hasCollider = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasPlayerController) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.45f, 0.7f, 1.0f));
        bool removePlayerController = false;
        bool changed = false;
        auto header = drawComponentHeader("Player Controller", "PlayerController", &obj.playerController.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removePlayerController = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("PlayerController");
            ImGui::Indent(10.0f);
            if (ImGui::DragFloat("Move Speed", &obj.playerController.moveSpeed, 0.1f, 0.1f, 100.0f, "%.2f")) {
                obj.playerController.moveSpeed = std::max(0.1f, obj.playerController.moveSpeed);
                changed = true;
            }
            if (ImGui::DragFloat("Look Sensitivity", &obj.playerController.lookSensitivity, 0.01f, 0.01f, 2.0f, "%.2f")) {
                obj.playerController.lookSensitivity = std::clamp(obj.playerController.lookSensitivity, 0.01f, 2.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Height", &obj.playerController.height, 0.01f, 0.5f, 3.0f, "%.2f")) {
                obj.playerController.height = std::clamp(obj.playerController.height, 0.5f, 3.0f);
                obj.scale.y = obj.playerController.height;
                obj.collider.boxSize.y = obj.playerController.height;
                changed = true;
            }
            if (ImGui::DragFloat("Radius", &obj.playerController.radius, 0.01f, 0.2f, 1.2f, "%.2f")) {
                obj.playerController.radius = std::clamp(obj.playerController.radius, 0.2f, 1.2f);
                obj.scale.x = obj.scale.z = obj.playerController.radius * 2.0f;
                obj.collider.boxSize.x = obj.collider.boxSize.z = obj.playerController.radius * 2.0f;
                changed = true;
            }
            if (ImGui::DragFloat("Jump Strength", &obj.playerController.jumpStrength, 0.1f, 0.1f, 30.0f, "%.1f")) {
                obj.playerController.jumpStrength = std::max(0.1f, obj.playerController.jumpStrength);
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removePlayerController) {
            obj.hasPlayerController = false;
            changed = true;
        }
        if (changed) {
            obj.hasCollider = true;
            obj.collider.type = ColliderType::Capsule;
            obj.collider.convex = true;
            obj.hasRigidbody = true;
            obj.rigidbody.enabled = true;
            obj.rigidbody.useGravity = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRigidbody) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.45f, 0.25f, 1.0f));
        bool removeRigidbody = false;
        bool changed = false;
        auto header = drawComponentHeader("Rigidbody", "Rigidbody", &obj.rigidbody.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeRigidbody = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Rigidbody");
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("Collider required for physics.");

            if (ImGui::DragFloat("Mass", &obj.rigidbody.mass, 0.05f, 0.01f, 1000.0f, "%.2f")) {
                obj.rigidbody.mass = std::max(0.01f, obj.rigidbody.mass);
                changed = true;
            }
            if (ImGui::Checkbox("Use Gravity", &obj.rigidbody.useGravity)) {
                changed = true;
            }
            if (ImGui::Checkbox("Kinematic", &obj.rigidbody.isKinematic)) {
                changed = true;
            }
            if (ImGui::DragFloat("Linear Damping", &obj.rigidbody.linearDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody.linearDamping = std::clamp(obj.rigidbody.linearDamping, 0.0f, 10.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Angular Damping", &obj.rigidbody.angularDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody.angularDamping = std::clamp(obj.rigidbody.angularDamping, 0.0f, 10.0f);
                changed = true;
            }
            ImGui::TextDisabled("Rotation Constraints");
            if (ImGui::Checkbox("Lock Rotation X", &obj.rigidbody.lockRotationX)) {
                changed = true;
            }
            if (ImGui::Checkbox("Lock Rotation Y", &obj.rigidbody.lockRotationY)) {
                changed = true;
            }
            if (ImGui::Checkbox("Lock Rotation Z", &obj.rigidbody.lockRotationZ)) {
                changed = true;
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeRigidbody) {
            obj.hasRigidbody = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasAudioSource) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.4f, 0.3f, 1.0f));
        bool removeAudioSource = false;
        bool changed = false;
        auto header = drawComponentHeader("Audio Source", "AudioSource", &obj.audioSource.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeAudioSource = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("AudioSource");
            ImGui::Indent(10.0f);
            auto& src = obj.audioSource;

            char clipBuf[512] = {};
            std::snprintf(clipBuf, sizeof(clipBuf), "%s", src.clipPath.c_str());
            ImGui::TextDisabled("Clip");
            ImGui::SetNextItemWidth(-170);
            if (ImGui::InputText("##ClipPath", clipBuf, sizeof(clipBuf))) {
                src.clipPath = clipBuf;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##AudioClip")) {
                src.clipPath.clear();
                changed = true;
            }
            ImGui::SameLine();
            bool selectionIsAudio = false;
            if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                selectionIsAudio = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Audio;
            }
            ImGui::BeginDisabled(!selectionIsAudio);
            if (ImGui::SmallButton("Use Selection##AudioClip")) {
                src.clipPath = fileBrowser.selectedFile.string();
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            bool previewPlaying = !src.clipPath.empty() && audio.isPreviewing(src.clipPath);
            if (ImGui::Button(previewPlaying ? "Stop Preview" : "Play Preview")) {
                if (previewPlaying) {
                    audio.stopPreview();
                } else if (!src.clipPath.empty()) {
                    audio.playPreview(src.clipPath, src.volume);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", src.clipPath.empty() ? "No clip selected" : fs::path(src.clipPath).filename().string().c_str());

            if (ImGui::SliderFloat("Volume", &src.volume, 0.0f, 1.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::Checkbox("Loop", &src.loop)) {
                changed = true;
            }
            if (ImGui::Checkbox("Play On Start", &src.playOnStart)) {
                changed = true;
            }
            if (ImGui::Checkbox("3D Spatialization", &src.spatial)) {
                changed = true;
            }
            ImGui::BeginDisabled(!src.spatial);
            if (ImGui::DragFloat("Min Distance", &src.minDistance, 0.1f, 0.1f, 200.0f, "%.2f")) {
                src.minDistance = std::max(0.1f, src.minDistance);
                changed = true;
            }
            if (ImGui::DragFloat("Max Distance", &src.maxDistance, 0.1f, src.minDistance + 0.5f, 500.0f, "%.2f")) {
                src.maxDistance = std::max(src.maxDistance, src.minDistance + 0.5f);
                changed = true;
            }
            ImGui::EndDisabled();

            const AudioClipPreview* clipPreview = audio.getPreview(src.clipPath);
            ImGui::Separator();
            ImGui::TextDisabled("Waveform");
            ImVec2 waveSize(ImGui::GetContentRegionAvail().x, 80.0f);
            double cur = 0.0;
            double dur = clipPreview ? clipPreview->durationSeconds : 0.0;
            float progress = -1.0f;
            if (audio.getPreviewTime(src.clipPath, cur, dur) && dur > 0.0001) {
                progress = static_cast<float>(cur / dur);
            }
            float seekRatio = -1.0f;
            drawWaveform("##AudioWaveComponent", clipPreview, waveSize, progress, &seekRatio);
            if (seekRatio >= 0.0f && dur > 0.0) {
                audio.seekPreview(src.clipPath, seekRatio * dur);
            }
            if (dur > 0.0) {
                ImGui::TextDisabled("Time: %0.2f / %0.2f", cur, dur);
            }
            if (clipPreview) {
                ImGui::TextDisabled("Length: %.2fs | %u channels @ %u Hz",
                    clipPreview->durationSeconds,
                    clipPreview->channels,
                    clipPreview->sampleRate);
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeAudioSource) {
            if (audio.isPreviewing(obj.audioSource.clipPath)) {
                audio.stopPreview();
            }
            obj.hasAudioSource = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::Camera) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.35f, 0.65f, 1.0f));
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("Camera");
            ImGui::Indent(10.0f);
            const char* cameraTypes[] = { "Scene", "Player" };
            int camType = static_cast<int>(obj.camera.type);
            if (ImGui::Combo("Type", &camType, cameraTypes, IM_ARRAYSIZE(cameraTypes))) {
                obj.camera.type = static_cast<SceneCameraType>(camType);
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            if (ImGui::SliderFloat("FOV", &obj.camera.fov, 20.0f, 120.0f, "%.0f deg")) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::DragFloat("Near Clip", &obj.camera.nearClip, 0.01f, 0.01f, obj.camera.farClip - 0.01f, "%.3f")) {
                obj.camera.nearClip = std::max(0.01f, std::min(obj.camera.nearClip, obj.camera.farClip - 0.01f));
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::DragFloat("Far Clip", &obj.camera.farClip, 0.1f, obj.camera.nearClip + 0.05f, 1000.0f, "%.1f")) {
                obj.camera.farClip = std::max(obj.camera.nearClip + 0.05f, obj.camera.farClip);
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::Checkbox("Apply Post Processing", &obj.camera.applyPostFX)) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::PostFXNode) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.6f, 1.0f));
        bool changed = false;
        auto header = drawComponentHeader("Post Processing", "PostFX", &obj.postFx.enabled, true, {});
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("PostFX");
            ImGui::Indent(10.0f);

            ImGui::Separator();
            ImGui::TextDisabled("Bloom");
            if (ImGui::Checkbox("Bloom Enabled", &obj.postFx.bloomEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.bloomEnabled);
            if (ImGui::SliderFloat("Threshold", &obj.postFx.bloomThreshold, 0.0f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Intensity", &obj.postFx.bloomIntensity, 0.0f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Spread", &obj.postFx.bloomRadius, 0.5f, 3.5f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Color Adjustments");
            if (ImGui::Checkbox("Enable Color Adjust", &obj.postFx.colorAdjustEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.colorAdjustEnabled);
            if (ImGui::SliderFloat("Exposure (EV)", &obj.postFx.exposure, -5.0f, 5.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Contrast", &obj.postFx.contrast, 0.0f, 2.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Saturation", &obj.postFx.saturation, 0.0f, 2.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::ColorEdit3("Color Filter", &obj.postFx.colorFilter.x)) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Motion Blur");
            if (ImGui::Checkbox("Enable Motion Blur", &obj.postFx.motionBlurEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.motionBlurEnabled);
            if (ImGui::SliderFloat("Strength", &obj.postFx.motionBlurStrength, 0.0f, 0.95f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Vignette");
            if (ImGui::Checkbox("Enable Vignette", &obj.postFx.vignetteEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.vignetteEnabled);
            if (ImGui::SliderFloat("Intensity", &obj.postFx.vignetteIntensity, 0.0f, 1.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Smoothness", &obj.postFx.vignetteSmoothness, 0.05f, 1.0f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Ambient Occlusion");
            if (ImGui::Checkbox("Enable AO", &obj.postFx.ambientOcclusionEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.ambientOcclusionEnabled);
            if (ImGui::SliderFloat("AO Radius", &obj.postFx.aoRadius, 0.0005f, 0.01f, "%.4f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("AO Strength", &obj.postFx.aoStrength, 0.0f, 2.0f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Chromatic Aberration");
            if (ImGui::Checkbox("Enable Chromatic", &obj.postFx.chromaticAberrationEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.chromaticAberrationEnabled);
            if (ImGui::SliderFloat("Fringe Amount", &obj.postFx.chromaticAmount, 0.0f, 0.01f, "%.4f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::TextDisabled("Nodes stack in hierarchy order; latest node overrides previous settings.");
            ImGui::TextDisabled("Wireframe/line mode auto-disables post effects.");
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    // Material section (skip for pure light objects)
    if (obj.type != ObjectType::DirectionalLight && obj.type != ObjectType::PointLight && obj.type != ObjectType::SpotLight && obj.type != ObjectType::AreaLight && obj.type != ObjectType::Camera && obj.type != ObjectType::PostFXNode) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.35f, 0.55f, 1.0f));

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("Material");
            ImGui::Indent(10.0f);

            auto textureField = [&](const char* label, const char* idSuffix, std::string& path) {
                bool changed = false;
                ImGui::PushID(idSuffix);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-160);
                char buf[512] = {};
                std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                    path = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    path.clear();
                    changed = true;
                }
                ImGui::SameLine();
                bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                 fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                ImGui::BeginDisabled(!canUseTex);
                std::string btnLabel = std::string("Use Selection##") + idSuffix;
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    path = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                return changed;
            };

            bool materialChanged = false;

            ImGui::TextColored(ImVec4(0.8f, 0.7f, 1.0f, 1.0f), "Surface Inputs");
            if (ImGui::ColorEdit3("Base Color", &obj.material.color.x)) {
                materialChanged = true;
            }

            float metallic = obj.material.specularStrength;
            if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                obj.material.specularStrength = metallic;
                materialChanged = true;
            }

            float smoothness = obj.material.shininess / 256.0f;
            if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
                smoothness = std::clamp(smoothness, 0.0f, 1.0f);
                obj.material.shininess = smoothness * 256.0f;
                materialChanged = true;
            }

            if (ImGui::SliderFloat("Ambient Light", &obj.material.ambientStrength, 0.0f, 1.0f)) {
                materialChanged = true;
            }
            if (ImGui::SliderFloat("Detail Mix", &obj.material.textureMix, 0.0f, 1.0f)) {
                materialChanged = true;
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Maps");
            materialChanged |= textureField("Base Map", "ObjAlbedo", obj.albedoTexturePath);
            if (ImGui::Checkbox("Use Detail Map", &obj.useOverlay)) {
                materialChanged = true;
            }
            materialChanged |= textureField("Detail Map", "ObjOverlay", obj.overlayTexturePath);
            materialChanged |= textureField("Normal Map", "ObjNormal", obj.normalMapPath);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "Shader");
            auto shaderField = [&](const char* label, const char* idSuffix, std::string& path) {
                bool changed = false;
                ImGui::PushID(idSuffix);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-160);
                char buf[512] = {};
                std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                    path = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    path.clear();
                    changed = true;
                }
                bool selectionIsShader = false;
                if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                    selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!selectionIsShader);
                std::string btn = std::string("Use Selection##") + idSuffix;
                if (ImGui::SmallButton(btn.c_str())) {
                    path = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                return changed;
            };
            materialChanged |= shaderField("Vertex Shader", "ObjVert", obj.vertexShaderPath);
            materialChanged |= shaderField("Fragment Shader", "ObjFrag", obj.fragmentShaderPath);

            ImGui::BeginDisabled(obj.vertexShaderPath.empty() && obj.fragmentShaderPath.empty());
            if (ImGui::Button("Reload Shader")) {
                renderer.forceReloadShader(obj.vertexShaderPath, obj.fragmentShaderPath);
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Material");
            ImGui::SameLine();
            ImVec4 previewColor(obj.material.color.x, obj.material.color.y, obj.material.color.z, 1.0f);
            ImVec2 sphereStart = ImGui::GetCursorScreenPos();
            float sphereRadius = 12.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(ImVec4(previewColor.x * 0.3f, previewColor.y * 0.3f, previewColor.z * 0.3f, 1.0f));
            ImU32 baseCol = ImGui::ColorConvertFloat4ToU32(previewColor);
            ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(1.0f, previewColor.x + 0.25f), std::min(1.0f, previewColor.y + 0.25f), std::min(1.0f, previewColor.z + 0.25f), 1.0f));
            ImVec2 center = ImVec2(sphereStart.x + sphereRadius, sphereStart.y + sphereRadius);
            dl->AddCircleFilled(center, sphereRadius, shadowCol);
            dl->AddCircleFilled(ImVec2(center.x, center.y - 1.5f), sphereRadius - 1.5f, baseCol);
            dl->AddCircleFilled(ImVec2(center.x - sphereRadius * 0.35f, center.y - sphereRadius * 0.5f), sphereRadius * 0.35f, highlightCol);
            ImGui::Dummy(ImVec2(sphereRadius * 2.0f, sphereRadius * 2.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", obj.materialPath.empty() ? "Unsaved Material" : fs::path(obj.materialPath).filename().string().c_str());
            ImGui::Text("Material Asset");

            char matPathBuf[512] = {};
            std::snprintf(matPathBuf, sizeof(matPathBuf), "%s", obj.materialPath.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##MaterialPath", matPathBuf, sizeof(matPathBuf))) {
                obj.materialPath = matPathBuf;
                materialChanged = true;
            }

            bool hasMatPath = obj.materialPath.size() > 0;
            ImGui::BeginDisabled(!hasMatPath);
            if (ImGui::Button("Save Material")) {
                saveMaterialToFile(obj);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Material")) {
                loadMaterialFromFile(obj);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!browserHasMaterial);
            if (ImGui::Button("Load Selected")) {
                obj.materialPath = selectedMaterialPath.string();
                loadMaterialFromFile(obj);
                materialChanged = true;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextDisabled("Material Slots");
            for (size_t slot = 0; slot < obj.additionalMaterialPaths.size(); ++slot) {
                ImGui::PushID(static_cast<int>(slot));
                char slotBuf[512] = {};
                std::snprintf(slotBuf, sizeof(slotBuf), "%s", obj.additionalMaterialPaths[slot].c_str());
                ImGui::SetNextItemWidth(-140);
                if (ImGui::InputText("##AdditionalMat", slotBuf, sizeof(slotBuf))) {
                    obj.additionalMaterialPaths[slot] = slotBuf;
                    materialChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Use Selection / Blender")) {
                    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                        fs::directory_entry entry(fileBrowser.selectedFile);
                        if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                            obj.additionalMaterialPaths[slot] = entry.path().string();
                            materialChanged = true;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    obj.additionalMaterialPaths.erase(obj.additionalMaterialPaths.begin() + static_cast<long>(slot));
                    materialChanged = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Material Slot")) {
                obj.additionalMaterialPaths.push_back("");
                materialChanged = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Preview");
            ImVec4 previewColorBar(obj.material.color.x, obj.material.color.y, obj.material.color.z, 1.0f);
            ImGui::ColorButton("##MaterialPreview", previewColorBar, ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetContentRegionAvail().x, 32.0f));

            if (materialChanged) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }

        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::DirectionalLight || obj.type == ObjectType::PointLight || obj.type == ObjectType::SpotLight || obj.type == ObjectType::AreaLight) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.45f, 0.2f, 1.0f));
        bool changed = false;
        auto header = drawComponentHeader("Light", "Light", &obj.light.enabled, true, {});
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Light");
            ImGui::Indent(10.0f);

            int currentType = (obj.type == ObjectType::DirectionalLight) ? 0 :
                              (obj.type == ObjectType::PointLight) ? 1 :
                              (obj.type == ObjectType::SpotLight) ? 2 : 3;
            const char* typeLabels[] = { "Directional", "Point", "Spot", "Area" };
            if (ImGui::Combo("Type", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels))) {
                if (currentType == 0) obj.type = ObjectType::DirectionalLight;
                else if (currentType == 1) obj.type = ObjectType::PointLight;
                else if (currentType == 2) obj.type = ObjectType::SpotLight;
                else obj.type = ObjectType::AreaLight;
                obj.light.type = (currentType == 0 ? LightType::Directional :
                                  currentType == 1 ? LightType::Point :
                                  currentType == 2 ? LightType::Spot : LightType::Area);
                // Reset sensible defaults when type changes
                if (obj.type == ObjectType::DirectionalLight) {
                    obj.light.intensity = 1.0f;
                } else if (obj.type == ObjectType::PointLight) {
                    obj.light.range = 12.0f;
                    obj.light.intensity = 2.0f;
                } else if (obj.type == ObjectType::SpotLight) {
                    obj.light.range = 15.0f;
                    obj.light.intensity = 2.5f;
                    obj.light.innerAngle = 15.0f;
                    obj.light.outerAngle = 25.0f;
                } else if (obj.type == ObjectType::AreaLight) {
                    obj.light.range = 10.0f;
                    obj.light.intensity = 3.0f;
                    obj.light.size = glm::vec2(2.0f, 2.0f);
                    obj.light.edgeFade = 0.2f;
                }
                changed = true;
            }

            if (ImGui::ColorEdit3("Color", &obj.light.color.x)) {
                changed = true;
            }
            if (ImGui::SliderFloat("Intensity", &obj.light.intensity, 0.0f, 10.0f)) {
                changed = true;
            }
            if (obj.type != ObjectType::DirectionalLight) {
                if (ImGui::SliderFloat("Range", &obj.light.range, 0.0f, 50.0f)) {
                    changed = true;
                }
            }

            if (obj.type == ObjectType::SpotLight) {
                if (ImGui::SliderFloat("Inner Angle", &obj.light.innerAngle, 1.0f, 90.0f)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Outer Angle", &obj.light.outerAngle, obj.light.innerAngle, 120.0f)) {
                    changed = true;
                }
            }

            if (obj.type == ObjectType::AreaLight) {
                if (ImGui::DragFloat2("Size", &obj.light.size.x, 0.05f, 0.1f, 10.0f)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Edge Softness", &obj.light.edgeFade, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::OBJMesh) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.4f, 1.0f));
        
        if (ImGui::CollapsingHeader("Mesh Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("MeshInfo");
            ImGui::Indent(10.0f);
            
            const auto* meshInfo = g_objLoader.getMeshInfo(obj.meshId);
            if (meshInfo) {
                ImGui::Text("Source File:");
                ImGui::TextDisabled("%s", fs::path(meshInfo->path).filename().string().c_str());
                
                ImGui::Spacing();
                
                ImGui::Text("Vertices: %d", meshInfo->vertexCount);
                ImGui::Text("Faces: %d", meshInfo->faceCount);
                ImGui::Text("Has Normals: %s", meshInfo->hasNormals ? "Yes" : "No");
                ImGui::Text("Has UVs: %s", meshInfo->hasTexCoords ? "Yes" : "No");
                
                ImGui::Spacing();
                
                if (ImGui::Button("Reload Mesh", ImVec2(-1, 0))) {
                    std::string errMsg;
                    int newId = g_objLoader.loadOBJ(obj.meshPath, errMsg);
                    if (newId >= 0) {
                        obj.meshId = newId;
                        addConsoleMessage("Reloaded mesh: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + errMsg, ConsoleMessageType::Error);
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Mesh data not found!");
                ImGui::TextDisabled("Path: %s", obj.meshPath.c_str());
                
                if (ImGui::Button("Try Reload", ImVec2(-1, 0))) {
                    std::string errMsg;
                    int newId = g_objLoader.loadOBJ(obj.meshPath, errMsg);
                    if (newId >= 0) {
                        obj.meshId = newId;
                        addConsoleMessage("Reloaded mesh: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + errMsg, ConsoleMessageType::Error);
                    }
                }
            }
            
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        
        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::Model) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.45f, 0.65f, 1.0f));

        if (ImGui::CollapsingHeader("Model Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("ModelInfo");
            ImGui::Indent(10.0f);

            const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId);
            if (meshInfo) {
                ImGui::Text("Source File:");
                ImGui::TextDisabled("%s", fs::path(meshInfo->path).filename().string().c_str());

                ImGui::Spacing();

                ImGui::Text("Vertices: %d", meshInfo->vertexCount);
                ImGui::Text("Faces: %d", meshInfo->faceCount);
                ImGui::Text("Has Normals: %s", meshInfo->hasNormals ? "Yes" : "No");
                ImGui::Text("Has UVs: %s", meshInfo->hasTexCoords ? "Yes" : "No");

                ImGui::Spacing();

                if (ImGui::Button("Reload Model", ImVec2(-1, 0))) {
                    ModelLoadResult result = getModelLoader().loadModel(obj.meshPath);
                    if (result.success) {
                        obj.meshId = result.meshIndex;
                        addConsoleMessage("Reloaded model: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + result.errorMessage, ConsoleMessageType::Error);
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Model data not found!");
                ImGui::TextDisabled("Path: %s", obj.meshPath.c_str());

                if (ImGui::Button("Try Reload", ImVec2(-1, 0))) {
                    ModelLoadResult result = getModelLoader().loadModel(obj.meshPath);
                    if (result.success) {
                        obj.meshId = result.meshIndex;
                        addConsoleMessage("Reloaded model: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + result.errorMessage, ConsoleMessageType::Error);
                    }
                }
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }

        ImGui::PopStyleColor();
    }

    bool scriptsChanged = false;
    int scriptToRemove = -1;

    for (size_t i = 0; i < obj.scripts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ScriptComponent& sc = obj.scripts[i];

        std::string headerLabel = sc.path.empty() ? "Script" : fs::path(sc.path).filename().string();
        std::string scriptId = "ScriptComponent" + std::to_string(i);
        auto header = drawComponentHeader(headerLabel.c_str(), scriptId.c_str(), &sc.enabled, true, [&]() {
            if (ImGui::MenuItem("Compile", nullptr, false, !sc.path.empty())) {
                compileScriptFile(sc.path);
            }
            if (ImGui::MenuItem("Remove")) {
                scriptToRemove = static_cast<int>(i);
            }
        });
        if (header.enabledChanged) {
            scriptsChanged = true;
        }

        if (scriptToRemove == static_cast<int>(i)) {
            ImGui::PopID();
            continue;
        }

        if (header.open) {
            char pathBuf[512] = {};
            std::snprintf(pathBuf, sizeof(pathBuf), "%s", sc.path.c_str());
            ImGui::TextDisabled("Path");
            ImGui::SetNextItemWidth(-140);
            if (ImGui::InputText("##ScriptPath", pathBuf, sizeof(pathBuf))) {
                sc.path = pathBuf;
                scriptsChanged = true;
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Use Selection")) {
                if (!fileBrowser.selectedFile.empty()) {
                    fs::directory_entry entry(fileBrowser.selectedFile);
                    if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
                        sc.path = entry.path().string();
                        scriptsChanged = true;
                    }
                }
            }

            if (!sc.path.empty()) {
                fs::path binary = resolveScriptBinary(sc.path);
                sc.lastBinaryPath = binary.string();
                ScriptRuntime::InspectorFn inspector = scriptRuntime.getInspector(binary);
                if (inspector) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Inspector (from script)");
                    ScriptContext ctx;
                    ctx.engine = this;
                    ctx.object = &obj;
                    ctx.script = &sc;
                    // Scope script inspector to avoid shared ImGui IDs across objects or multiple instances
                    std::string inspectorId = "ScriptInspector##" + std::to_string(obj.id) + sc.path;
                    ImGui::PushID(inspectorId.c_str());
                    inspector(ctx);
                    ImGui::PopID();
                } else if (!scriptRuntime.getLastError().empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Inspector load failed");
                    ImGui::TextWrapped("%s", scriptRuntime.getLastError().c_str());
                } else {
                    ImGui::TextDisabled("No inspector exported (Script_OnInspector)");
                }
            }

            ImGui::TextDisabled("Settings");
            for (size_t s = 0; s < sc.settings.size(); ++s) {
                ImGui::PushID(static_cast<int>(s));
                char keyBuf[128] = {};
                char valBuf[256] = {};
                std::snprintf(keyBuf, sizeof(keyBuf), "%s", sc.settings[s].key.c_str());
                std::snprintf(valBuf, sizeof(valBuf), "%s", sc.settings[s].value.c_str());
                auto isBoolString = [](const std::string& v, bool& out) {
                    if (v == "1" || v == "true" || v == "True") { out = true; return true; }
                    if (v == "0" || v == "false" || v == "False") { out = false; return true; }
                    return false;
                };
                auto isNumberString = [](const std::string& v, float& out) {
                    if (v.empty()) return false;
                    char* end = nullptr;
                    out = std::strtof(v.c_str(), &end);
                    return end && *end == '\0';
                };
                bool boolVal = false;
                bool hasBool = isBoolString(sc.settings[s].value, boolVal);
                float numVal = 0.0f;
                bool hasNumber = isNumberString(sc.settings[s].value, numVal);
                ImGui::SetNextItemWidth(140);
                if (ImGui::InputText("##Key", keyBuf, sizeof(keyBuf))) {
                    sc.settings[s].key = keyBuf;
                    scriptsChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-200);
                if (hasBool) {
                    if (ImGui::Checkbox("##BoolVal", &boolVal)) {
                        sc.settings[s].value = boolVal ? "1" : "0";
                        scriptsChanged = true;
                    }
                } else if (hasNumber) {
                    if (ImGui::InputFloat("##NumVal", &numVal, 0.0f, 0.0f, "%.4f")) {
                        sc.settings[s].value = std::to_string(numVal);
                        scriptsChanged = true;
                    }
                } else {
                    if (ImGui::InputText("##Value", valBuf, sizeof(valBuf))) {
                        sc.settings[s].value = valBuf;
                        scriptsChanged = true;
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(hasBool);
                if (ImGui::SmallButton("As Bool")) {
                    sc.settings[s].value = (!sc.settings[s].value.empty() && sc.settings[s].value != "0" && sc.settings[s].value != "false") ? "1" : "0";
                    scriptsChanged = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(hasNumber);
                if (ImGui::SmallButton("As Number")) {
                    float parsed = 0.0f;
                    if (!isNumberString(sc.settings[s].value, parsed)) parsed = 0.0f;
                    sc.settings[s].value = std::to_string(parsed);
                    scriptsChanged = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    sc.settings.erase(sc.settings.begin() + static_cast<long>(s));
                    scriptsChanged = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            if (ImGui::SmallButton("Add Setting")) {
                sc.settings.push_back(ScriptSetting{"", ""});
                scriptsChanged = true;
            }
        }

        ImGui::PopID();
    }

    if (scriptToRemove >= 0 && scriptToRemove < static_cast<int>(obj.scripts.size())) {
        obj.scripts.erase(obj.scripts.begin() + scriptToRemove);
        scriptsChanged = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    bool componentChanged = false;
    ImGui::PushID("AddComponent");
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!obj.hasRigidbody && ImGui::MenuItem("Rigidbody")) {
            obj.hasRigidbody = true;
            obj.rigidbody = RigidbodyComponent{};
            componentChanged = true;
        }
        if (!obj.hasPlayerController && ImGui::MenuItem("Player Controller")) {
            obj.hasPlayerController = true;
            obj.playerController = PlayerControllerComponent{};
            obj.hasCollider = true;
            obj.collider.type = ColliderType::Capsule;
            obj.collider.boxSize = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
            obj.collider.convex = true;
            obj.hasRigidbody = true;
            obj.rigidbody.enabled = true;
            obj.rigidbody.useGravity = true;
            obj.rigidbody.isKinematic = false;
            obj.scale = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
            componentChanged = true;
        }
        if (!obj.hasAudioSource && ImGui::MenuItem("Audio Source")) {
            obj.hasAudioSource = true;
            obj.audioSource = AudioSourceComponent{};
            componentChanged = true;
        }
        if (!obj.hasCollider && ImGui::BeginMenu("Collider")) {
            if (ImGui::MenuItem("Box Collider")) {
                obj.hasCollider = true;
                obj.collider = ColliderComponent{};
                obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                componentChanged = true;
            }
            if (ImGui::MenuItem("Mesh Collider (Triangle)")) {
                obj.hasCollider = true;
                obj.collider = ColliderComponent{};
                obj.collider.type = ColliderType::Mesh;
                obj.collider.convex = false;
                componentChanged = true;
            }
            if (ImGui::MenuItem("Mesh Collider (Convex)")) {
                obj.hasCollider = true;
                obj.collider = ColliderComponent{};
                obj.collider.type = ColliderType::ConvexMesh;
                obj.collider.convex = true;
                componentChanged = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Script")) {
            obj.scripts.push_back(ScriptComponent{});
            scriptsChanged = true;
            componentChanged = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();

    if (scriptsChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    if (componentChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    if (browserHasAudio) {
        ImGui::Spacing();
        renderAudioAssetPanel("Audio Clip (File Browser)", &obj);
    }
    if (browserHasMaterial) {
        ImGui::Spacing();
        renderMaterialAssetPanel("Material Asset (File Browser)", true);
    }

    ImGui::PopID(); // object scope
    ImGui::End();
}

void Engine::renderConsolePanel() {
    ImGui::Begin("Console", &showConsole);

    if (ImGui::Button("Clear")) {
        consoleLog.clear();
    }

    ImGui::SameLine();
    static bool autoScroll = true;
    ImGui::Checkbox("Auto-scroll", &autoScroll);

    ImGui::Separator();

    ImGui::BeginChild("ConsoleOutput", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& log : consoleLog) {
        ImVec4 color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        if (log.find("Error") != std::string::npos) {
            color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        } else if (log.find("Warning") != std::string::npos) {
            color = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
        } else if (log.find("Success") != std::string::npos) {
            color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
        }
        ImGui::TextColored(color, "%s", log.c_str());
    }

    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    ImGui::End();
}

void Engine::renderMeshBuilderPanel() {
    ImGui::Begin("Mesh Builder", &showMeshBuilder);

    auto recalcBounds = [this]() {
        if (!meshBuilder.hasMesh || meshBuilder.mesh.positions.empty()) return;
        meshBuilder.mesh.boundsMin = glm::vec3(FLT_MAX);
        meshBuilder.mesh.boundsMax = glm::vec3(-FLT_MAX);
        for (const auto& p : meshBuilder.mesh.positions) {
            meshBuilder.mesh.boundsMin.x = std::min(meshBuilder.mesh.boundsMin.x, p.x);
            meshBuilder.mesh.boundsMin.y = std::min(meshBuilder.mesh.boundsMin.y, p.y);
            meshBuilder.mesh.boundsMin.z = std::min(meshBuilder.mesh.boundsMin.z, p.z);
            meshBuilder.mesh.boundsMax.x = std::max(meshBuilder.mesh.boundsMax.x, p.x);
            meshBuilder.mesh.boundsMax.y = std::max(meshBuilder.mesh.boundsMax.y, p.y);
            meshBuilder.mesh.boundsMax.z = std::max(meshBuilder.mesh.boundsMax.z, p.z);
        }
    };

    ImGui::InputText("Mesh Path", meshBuilderPath, sizeof(meshBuilderPath));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (!meshBuilder.load(meshBuilderPath, err)) {
            addConsoleMessage("MeshBuilder load failed: " + err, ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Loaded raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string err;
        std::string path = strlen(meshBuilderPath) ? meshBuilderPath : meshBuilder.loadedPath;
        if (!meshBuilder.save(path, err)) {
            addConsoleMessage("MeshBuilder save failed: " + err, ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Saved raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
            strncpy(meshBuilderPath, meshBuilder.loadedPath.c_str(), sizeof(meshBuilderPath) - 1);
            meshBuilderPath[sizeof(meshBuilderPath) - 1] = '\0';
        }
    }

    if (ImGui::Button("Load Selected File")) {
        if (!fileBrowser.selectedFile.empty() && fs::path(fileBrowser.selectedFile).extension() == ".rmesh") {
            strncpy(meshBuilderPath, fileBrowser.selectedFile.string().c_str(), sizeof(meshBuilderPath) - 1);
            meshBuilderPath[sizeof(meshBuilderPath) - 1] = '\0';
            std::string err;
            if (!meshBuilder.load(meshBuilderPath, err)) {
                addConsoleMessage("MeshBuilder load failed: " + err, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Loaded raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
            }
        } else {
            addConsoleMessage("Select a .rmesh file in the browser to load", ConsoleMessageType::Warning);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Recompute Normals")) {
        meshBuilder.recomputeNormals();
    }

    ImGui::Separator();

    if (!meshBuilder.hasMesh) {
        ImGui::TextDisabled("No mesh loaded.");
        ImGui::End();
        return;
    }

    ImGui::Text("Vertices: %zu", meshBuilder.mesh.positions.size());
    ImGui::Text("Faces: %zu", meshBuilder.mesh.faces.size());
    ImGui::Text("Bounds Min: (%.3f, %.3f, %.3f)", meshBuilder.mesh.boundsMin.x, meshBuilder.mesh.boundsMin.y, meshBuilder.mesh.boundsMin.z);
    ImGui::Text("Bounds Max: (%.3f, %.3f, %.3f)", meshBuilder.mesh.boundsMax.x, meshBuilder.mesh.boundsMax.y, meshBuilder.mesh.boundsMax.z);
    if (meshBuilder.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0.7f,0.2f,1),"*modified");
    }

    ImGui::SeparatorText("Vertices");
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Selected", &meshBuilder.selectedVertex);
    if (meshBuilder.selectedVertex < 0 || meshBuilder.selectedVertex >= (int)meshBuilder.mesh.positions.size()) {
        meshBuilder.selectedVertex = meshBuilder.mesh.positions.empty() ? -1 : 0;
    }

    if (meshBuilder.selectedVertex >= 0 && meshBuilder.selectedVertex < (int)meshBuilder.mesh.positions.size()) {
        glm::vec3& pos = meshBuilder.mesh.positions[meshBuilder.selectedVertex];
        float edit[3] = { pos.x, pos.y, pos.z };
        if (ImGui::InputFloat3("Position", edit, "%.4f")) {
            pos = glm::vec3(edit[0], edit[1], edit[2]);
            recalcBounds();
            meshBuilder.recomputeNormals();
            meshBuilder.dirty = true;
        }
        if (meshBuilder.mesh.hasUVs && meshBuilder.selectedVertex < (int)meshBuilder.mesh.uvs.size()) {
            glm::vec2& uv = meshBuilder.mesh.uvs[meshBuilder.selectedVertex];
            float uvEdit[2] = { uv.x, uv.y };
            if (ImGui::InputFloat2("UV", uvEdit, "%.4f")) {
                uv = glm::vec2(uvEdit[0], uvEdit[1]);
                meshBuilder.dirty = true;
            }
        }
    }

    ImGui::SeparatorText("Add Face / Fill");
    ImGui::InputTextWithHint("Indices", "e.g. 0,1,2 or 0 1 2 3", meshBuilderFaceInput, sizeof(meshBuilderFaceInput));
    ImGui::SameLine();
    if (ImGui::Button("Add Face")) {
        std::vector<uint32_t> indices;
        std::string token;
        std::stringstream ss(meshBuilderFaceInput);
        while (std::getline(ss, token, ',')) {
            std::stringstream inner(token);
            std::string sub;
            while (inner >> sub) {
                try {
                    uint32_t idx = static_cast<uint32_t>(std::stoul(sub));
                    indices.push_back(idx);
                } catch (...) {}
            }
        }
        if (indices.empty()) {
            addConsoleMessage("Enter vertex indices separated by commas or spaces", ConsoleMessageType::Warning);
        } else {
            std::string err;
            if (!meshBuilder.addFace(indices, err)) {
                addConsoleMessage("Add face failed: " + err, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Added face with " + std::to_string(indices.size()) + " verts", ConsoleMessageType::Success);
            }
        }
    }

    ImGui::SeparatorText("Faces (first 16)");
    int maxFaces = std::min<int>(16, meshBuilder.mesh.faces.size());
    for (int i = 0; i < maxFaces; i++) {
        const auto& f = meshBuilder.mesh.faces[i];
        ImGui::Text("%d: %u, %u, %u", i, f.x, f.y, f.z);
    }

    ImGui::End();
}

void Engine::renderDialogs() {
    if (showNewSceneDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 130), ImGuiCond_Appearing);

        if (ImGui::Begin("New Scene", &showNewSceneDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("Scene Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##NewSceneName", newSceneName, sizeof(newSceneName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showNewSceneDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
                if (strlen(newSceneName) > 0) {
                    createNewScene(newSceneName);
                    showNewSceneDialog = false;
                    memset(newSceneName, 0, sizeof(newSceneName));
                }
            }
        }
        ImGui::End();
    }

    if (showCompilePopup) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("Script Compile", &showCompilePopup, flags)) {
            ImGui::TextWrapped("%s", lastCompileStatus.c_str());
            ImGui::Separator();
            ImGui::BeginChild("CompileLog", ImVec2(0, -40), true);
            ImGui::TextUnformatted(lastCompileLog.c_str());
            ImGui::EndChild();
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(80, 0))) {
                showCompilePopup = false;
            }
        }
        ImGui::End();
    }

    if (showSaveSceneAsDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 130), ImGuiCond_Appearing);

        if (ImGui::Begin("Save Scene As", &showSaveSceneAsDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("Scene Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##SaveSceneAsName", saveSceneAsName, sizeof(saveSceneAsName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showSaveSceneAsDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(buttonWidth, 0))) {
                if (strlen(saveSceneAsName) > 0) {
                    projectManager.currentProject.currentSceneName = saveSceneAsName;
                    saveCurrentScene();
                    showSaveSceneAsDialog = false;
                    memset(saveSceneAsName, 0, sizeof(saveSceneAsName));
                }
            }
        }
        ImGui::End();
    }
    
    // OBJ Import dialog
    if (showImportOBJDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 160), ImGuiCond_Appearing);

        if (ImGui::Begin("Import OBJ Model", &showImportOBJDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingOBJPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingOBJPath.c_str());
            
            ImGui::Spacing();
            
            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportOBJName", importOBJName, sizeof(importOBJName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportOBJDialog = false;
                pendingOBJPath.clear();
            }
            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                importOBJToScene(pendingOBJPath, importOBJName);
                showImportOBJDialog = false;
                pendingOBJPath.clear();
                memset(importOBJName, 0, sizeof(importOBJName));
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }

    // General model import dialog (Assimp-backed)
    if (showImportModelDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_Appearing);

        if (ImGui::Begin("Import Model", &showImportModelDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingModelPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingModelPath.c_str());

            ImGui::Spacing();

            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportModelName", importModelName, sizeof(importModelName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportModelDialog = false;
                pendingModelPath.clear();
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                importModelToScene(pendingModelPath, importModelName);
                showImportModelDialog = false;
                pendingModelPath.clear();
                memset(importModelName, 0, sizeof(importModelName));
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }
}
