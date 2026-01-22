#include "Engine.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

void Engine::renderAnimationWindow() {
    if (!showAnimationWindow) return;

    auto clampFloat = [](float value, float minValue, float maxValue) {
        return std::max(minValue, std::min(value, maxValue));
    };

    auto lerpVec3 = [](const glm::vec3& a, const glm::vec3& b, float t) {
        return a + (b - a) * t;
    };

    auto applyInterpolation = [](float t, AnimationInterpolation interpolation) {
        t = std::max(0.0f, std::min(1.0f, t));
        switch (interpolation) {
            case AnimationInterpolation::SmoothStep:
                return t * t * (3.0f - 2.0f * t);
            case AnimationInterpolation::EaseIn:
                return t * t;
            case AnimationInterpolation::EaseOut: {
                float inv = 1.0f - t;
                return 1.0f - inv * inv;
            }
            case AnimationInterpolation::EaseInOut:
                return (t < 0.5f) ? (2.0f * t * t) : (1.0f - 2.0f * (1.0f - t) * (1.0f - t));
            case AnimationInterpolation::Linear:
            default:
                return t;
        }
    };

    const char* interpLabels[] = { "Linear", "SmoothStep", "Ease In", "Ease Out", "Ease In Out" };
    const char* curveModeLabels[] = { "Preset", "Bezier" };

    auto getInterpLabel = [&](AnimationInterpolation interpolation) {
        int idx = static_cast<int>(interpolation);
        if (idx < 0 || idx >= static_cast<int>(IM_ARRAYSIZE(interpLabels))) return "Linear";
        return interpLabels[idx];
    };

    auto cubicBezier = [](float p0, float p1, float p2, float p3, float t) {
        float inv = 1.0f - t;
        return (inv * inv * inv * p0) +
               (3.0f * inv * inv * t * p1) +
               (3.0f * inv * t * t * p2) +
               (t * t * t * p3);
    };

    auto cubicBezierDerivative = [](float p0, float p1, float p2, float p3, float t) {
        float inv = 1.0f - t;
        return (3.0f * inv * inv * (p1 - p0)) +
               (6.0f * inv * t * (p2 - p1)) +
               (3.0f * t * t * (p3 - p2));
    };

    auto applyBezier = [&](float t, const glm::vec2& outCtrl, const glm::vec2& inCtrl) {
        t = std::max(0.0f, std::min(1.0f, t));
        float u = t;
        for (int i = 0; i < 6; ++i) {
            float x = cubicBezier(0.0f, outCtrl.x, inCtrl.x, 1.0f, u);
            float dx = cubicBezierDerivative(0.0f, outCtrl.x, inCtrl.x, 1.0f, u);
            if (std::abs(dx) < 0.0001f) break;
            u -= (x - t) / dx;
            u = std::max(0.0f, std::min(1.0f, u));
        }
        float xCheck = cubicBezier(0.0f, outCtrl.x, inCtrl.x, 1.0f, u);
        if (std::abs(xCheck - t) > 0.001f) {
            float lo = 0.0f;
            float hi = 1.0f;
            for (int i = 0; i < 12; ++i) {
                float mid = (lo + hi) * 0.5f;
                float x = cubicBezier(0.0f, outCtrl.x, inCtrl.x, 1.0f, mid);
                if (x < t) lo = mid;
                else hi = mid;
            }
            u = (lo + hi) * 0.5f;
        }
        return cubicBezier(0.0f, outCtrl.y, inCtrl.y, 1.0f, u);
    };

    auto captureKeyframe = [&](SceneObject& obj) {
        auto& anim = obj.animation;
        float clamped = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
        auto it = std::find_if(anim.keyframes.begin(), anim.keyframes.end(),
                               [&](const AnimationKeyframe& k) { return std::abs(k.time - clamped) < 0.0001f; });
        if (it == anim.keyframes.end()) {
            AnimationKeyframe key;
            key.time = clamped;
            key.position = obj.position;
            key.rotation = obj.rotation;
            key.scale = obj.scale;
            key.interpolation = AnimationInterpolation::SmoothStep;
            key.curveMode = AnimationCurveMode::Preset;
            anim.keyframes.push_back(key);
        } else {
            it->position = obj.position;
            it->rotation = obj.rotation;
            it->scale = obj.scale;
        }
        std::sort(anim.keyframes.begin(), anim.keyframes.end(),
                  [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; });
        projectManager.currentProject.hasUnsavedChanges = true;
    };

    auto deleteKeyframe = [&](SceneObject& obj) {
        auto& anim = obj.animation;
        if (animationSelectedKey < 0 || animationSelectedKey >= static_cast<int>(anim.keyframes.size())) return;
        anim.keyframes.erase(anim.keyframes.begin() + animationSelectedKey);
        if (animationSelectedKey >= static_cast<int>(anim.keyframes.size())) {
            animationSelectedKey = static_cast<int>(anim.keyframes.size()) - 1;
        }
        projectManager.currentProject.hasUnsavedChanges = true;
    };

    auto applyPoseAtTime = [&](SceneObject& obj, float time) {
        auto& anim = obj.animation;
        if (anim.keyframes.empty()) return;

        if (time <= anim.keyframes.front().time) {
            obj.position = anim.keyframes.front().position;
            obj.rotation = NormalizeEulerDegrees(anim.keyframes.front().rotation);
            obj.scale = anim.keyframes.front().scale;
            syncLocalTransform(obj);
            projectManager.currentProject.hasUnsavedChanges = true;
            return;
        }
        if (time >= anim.keyframes.back().time) {
            obj.position = anim.keyframes.back().position;
            obj.rotation = NormalizeEulerDegrees(anim.keyframes.back().rotation);
            obj.scale = anim.keyframes.back().scale;
            syncLocalTransform(obj);
            projectManager.currentProject.hasUnsavedChanges = true;
            return;
        }

        for (size_t i = 0; i + 1 < anim.keyframes.size(); ++i) {
            const auto& a = anim.keyframes[i];
            const auto& b = anim.keyframes[i + 1];
            if (time >= a.time && time <= b.time) {
                float span = b.time - a.time;
                float t = (span > 0.0f) ? (time - a.time) / span : 0.0f;
                if (a.curveMode == AnimationCurveMode::Bezier) {
                    t = applyBezier(t, a.bezierOut, b.bezierIn);
                } else {
                    t = applyInterpolation(t, a.interpolation);
                }
                obj.position = lerpVec3(a.position, b.position, t);
                obj.rotation = NormalizeEulerDegrees(lerpVec3(a.rotation, b.rotation, t));
                obj.scale = lerpVec3(a.scale, b.scale, t);
                syncLocalTransform(obj);
                projectManager.currentProject.hasUnsavedChanges = true;
                return;
            }
        }
    };

    auto drawTimeline = [&](AnimationComponent& anim) {
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 70.0f);
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("AnimationTimeline", size);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
        ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_SliderGrab);

        draw->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), bg, 6.0f);
        draw->AddRect(start, ImVec2(start.x + size.x, start.y + size.y), border, 6.0f);

        float clamped = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
        float playheadX = start.x + (anim.clipLength > 0.0f ? (clamped / anim.clipLength) * size.x : 0.0f);
        draw->AddLine(ImVec2(playheadX, start.y), ImVec2(playheadX, start.y + size.y), accent, 2.0f);

        for (size_t i = 0; i < anim.keyframes.size(); ++i) {
            float keyX = start.x +
                (anim.clipLength > 0.0f ? (anim.keyframes[i].time / anim.clipLength) * size.x : 0.0f);
            ImVec2 center(keyX, start.y + size.y * 0.5f);
            float radius = (animationSelectedKey == static_cast<int>(i)) ? 6.0f : 4.5f;
            draw->AddCircleFilled(center, radius, keyColor);

            ImRect hit(ImVec2(center.x - 7.0f, center.y - 7.0f), ImVec2(center.x + 7.0f, center.y + 7.0f));
            if (ImGui::IsMouseHoveringRect(hit.Min, hit.Max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                animationSelectedKey = static_cast<int>(i);
                animationCurrentTime = anim.keyframes[i].time;
            }
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float t = (mouseX - start.x) / size.x;
            animationCurrentTime = clampFloat(t * anim.clipLength, 0.0f, anim.clipLength);
        }
    };

    auto* selectedObj = getSelectedObject();
    std::vector<SceneObject*> animTargets;
    animTargets.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        if (obj.hasAnimation) animTargets.push_back(&obj);
    }

    auto resolveTarget = [&]() -> SceneObject* {
        if (animationTargetId < 0) return nullptr;
        SceneObject* obj = findObjectById(animationTargetId);
        if (!obj || !obj->hasAnimation) return nullptr;
        return obj;
    };

    SceneObject* targetObj = resolveTarget();
    if (!targetObj && !animTargets.empty()) {
        animationTargetId = animTargets.front()->id;
        animationSelectedKey = -1;
        animationLastAppliedTime = -1.0f;
        targetObj = resolveTarget();
    }

    ImGui::Begin("Animation", &showAnimationWindow, ImGuiWindowFlags_NoCollapse);

    if (!ImGui::BeginTable("AnimatorLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::End();
        return;
    }

    ImGui::TableSetupColumn("Targets", ImGuiTableColumnFlags_WidthFixed, 220.0f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::BeginChild("AnimatorTargets", ImVec2(0, 0), true);
    ImGui::TextDisabled("Targets");
    ImGui::Spacing();
    ImGui::BeginDisabled(!selectedObj);
    if (ImGui::Button("Add Animation to Selected", ImVec2(-1, 0))) {
        if (selectedObj && !selectedObj->hasAnimation) {
            selectedObj->hasAnimation = true;
            selectedObj->animation = AnimationComponent{};
            projectManager.currentProject.hasUnsavedChanges = true;
            animationTargetId = selectedObj->id;
            animationSelectedKey = -1;
            animationLastAppliedTime = -1.0f;
            animTargets.push_back(selectedObj);
        } else if (selectedObj) {
            animationTargetId = selectedObj->id;
        }
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (animTargets.empty()) {
        ImGui::TextDisabled("No Animation components yet.");
    } else {
        for (auto* obj : animTargets) {
            bool selected = (targetObj && obj->id == targetObj->id);
            if (ImGui::Selectable(obj->name.c_str(), selected)) {
                animationTargetId = obj->id;
                animationSelectedKey = -1;
                animationLastAppliedTime = -1.0f;
                animationCurrentTime = 0.0f;
                targetObj = obj;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Keyframes: %zu", obj->animation.keyframes.size());
                ImGui::Text("Length: %.2fs", obj->animation.clipLength);
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    ImGui::BeginChild("AnimatorEditor", ImVec2(0, 0), true);

    if (!targetObj) {
        ImGui::TextDisabled("Select or add an Animation component to edit.");
        ImGui::EndChild();
        ImGui::EndTable();
        ImGui::End();
        return;
    }

    auto& anim = targetObj->animation;
    animationCurrentTime = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);

    ImGui::Text("Animator");
    ImGui::SameLine();
    ImGui::TextDisabled("Target: %s", targetObj->name.c_str());

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginTabBar("AnimatorTabs")) {
        if (ImGui::BeginTabItem("Pose")) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::BeginDisabled(!anim.enabled);
            if (ImGui::Button("Key")) {
                captureKeyframe(*targetObj);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(animationSelectedKey < 0);
            if (ImGui::Button("Delete")) {
                deleteKeyframe(*targetObj);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(anim.keyframes.empty());
            if (ImGui::Button("Sort")) {
                std::sort(anim.keyframes.begin(), anim.keyframes.end(),
                          [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; });
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::EndDisabled();
            ImGui::PopStyleVar();

            ImGui::Spacing();
            drawTimeline(anim);
            ImGui::SliderFloat("Time", &animationCurrentTime, 0.0f, anim.clipLength, "%.2fs");

            if (animationSelectedKey >= 0 && animationSelectedKey < static_cast<int>(anim.keyframes.size())) {
                auto& key = anim.keyframes[animationSelectedKey];
                ImGui::Separator();
                ImGui::TextDisabled("Blend");
                int modeIndex = static_cast<int>(key.curveMode);
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo("Mode", &modeIndex, curveModeLabels, IM_ARRAYSIZE(curveModeLabels))) {
                    key.curveMode = static_cast<AnimationCurveMode>(modeIndex);
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply Mode To All")) {
                    for (auto& k : anim.keyframes) {
                        k.curveMode = key.curveMode;
                    }
                    projectManager.currentProject.hasUnsavedChanges = true;
                }

                if (key.curveMode == AnimationCurveMode::Preset) {
                    int interpIndex = static_cast<int>(key.interpolation);
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::Combo("Preset", &interpIndex, interpLabels, IM_ARRAYSIZE(interpLabels))) {
                        key.interpolation = static_cast<AnimationInterpolation>(interpIndex);
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Apply Preset To All")) {
                        for (auto& k : anim.keyframes) {
                            k.interpolation = key.interpolation;
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                } else {
                    ImGui::TextDisabled("Out Handle (to next)");
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::SliderFloat2("Out", &key.bezierOut.x, 0.0f, 1.0f, "%.2f")) {
                        key.bezierOut.x = clampFloat(key.bezierOut.x, 0.0f, 1.0f);
                        key.bezierOut.y = clampFloat(key.bezierOut.y, 0.0f, 1.0f);
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::TextDisabled("In Handle (from prev)");
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::SliderFloat2("In", &key.bezierIn.x, 0.0f, 1.0f, "%.2f")) {
                        key.bezierIn.x = clampFloat(key.bezierIn.x, 0.0f, 1.0f);
                        key.bezierIn.y = clampFloat(key.bezierIn.y, 0.0f, 1.0f);
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }

                    int nextIndex = animationSelectedKey + 1;
                    if (nextIndex < static_cast<int>(anim.keyframes.size())) {
                        static int activeHandle = -1;
                        auto& nextKey = anim.keyframes[nextIndex];
                        ImVec2 previewSize(260.0f, 110.0f);
                        ImGui::TextDisabled("Curve Editor");
                        ImGui::BeginChild("BezierPreview", previewSize, true, ImGuiWindowFlags_NoScrollbar);
                        ImDrawList* draw = ImGui::GetWindowDrawList();
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImVec2 p1(p0.x + previewSize.x, p0.y + previewSize.y);
                        ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border);
                        ImU32 handleColor = ImGui::GetColorU32(ImGuiCol_SliderGrab);
                        ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_CheckMark);
                        draw->AddRect(p0, p1, grid);

                        auto toScreen = [&](const glm::vec2& v) {
                            return ImVec2(p0.x + v.x * previewSize.x, p0.y + (1.0f - v.y) * previewSize.y);
                        };

                        ImVec2 outHandle = toScreen(key.bezierOut);
                        ImVec2 inHandle = toScreen(nextKey.bezierIn);
                        ImVec2 start = toScreen(glm::vec2(0.0f, 0.0f));
                        ImVec2 end = toScreen(glm::vec2(1.0f, 1.0f));
                        draw->AddLine(start, outHandle, grid, 1.0f);
                        draw->AddLine(end, inHandle, grid, 1.0f);
                        draw->AddCircleFilled(outHandle, 5.0f, handleColor);
                        draw->AddCircleFilled(inHandle, 5.0f, handleColor);

                        const int samples = 32;
                        ImVec2 last = start;
                        for (int i = 0; i <= samples; ++i) {
                            float t = static_cast<float>(i) / samples;
                            float y = applyBezier(t, key.bezierOut, nextKey.bezierIn);
                            ImVec2 cur(p0.x + t * previewSize.x, p0.y + (1.0f - y) * previewSize.y);
                            if (i > 0) {
                                draw->AddLine(last, cur, lineColor, 2.0f);
                            }
                            last = cur;
                        }

                        ImRect outRect(ImVec2(outHandle.x - 7.0f, outHandle.y - 7.0f),
                                       ImVec2(outHandle.x + 7.0f, outHandle.y + 7.0f));
                        ImRect inRect(ImVec2(inHandle.x - 7.0f, inHandle.y - 7.0f),
                                      ImVec2(inHandle.x + 7.0f, inHandle.y + 7.0f));

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            if (ImGui::IsMouseHoveringRect(outRect.Min, outRect.Max)) activeHandle = 0;
                            else if (ImGui::IsMouseHoveringRect(inRect.Min, inRect.Max)) activeHandle = 1;
                            else activeHandle = -1;
                        }
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                            activeHandle = -1;
                        }

                        if (activeHandle >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                            ImVec2 mouse = ImGui::GetIO().MousePos;
                            float x = (mouse.x - p0.x) / previewSize.x;
                            float y = 1.0f - (mouse.y - p0.y) / previewSize.y;
                            glm::vec2 clamped(clampFloat(x, 0.0f, 1.0f), clampFloat(y, 0.0f, 1.0f));
                            if (activeHandle == 0) {
                                key.bezierOut = clamped;
                            } else {
                                nextKey.bezierIn = clamped;
                            }
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }

                        ImGui::EndChild();
                    }
                }
            }

            ImGui::Spacing();
            if (anim.keyframes.empty()) {
                ImGui::TextDisabled("No keyframes yet.");
            } else if (ImGui::BeginTable("AnimationKeyframeTable", 5,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Blend");
                ImGui::TableSetupColumn("Position");
                ImGui::TableSetupColumn("Rotation");
                ImGui::TableSetupColumn("Scale");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < anim.keyframes.size(); ++i) {
                    const auto& key = anim.keyframes[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    bool selected = animationSelectedKey == static_cast<int>(i);
                    char label[32];
                    std::snprintf(label, sizeof(label), "%.2f", key.time);
                    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        animationSelectedKey = static_cast<int>(i);
                        animationCurrentTime = key.time;
                    }
                    ImGui::TableNextColumn();
                    if (key.curveMode == AnimationCurveMode::Bezier) {
                        ImGui::TextUnformatted("Bezier");
                    } else {
                        ImGui::TextUnformatted(getInterpLabel(key.interpolation));
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f, %.2f, %.2f", key.position.x, key.position.y, key.position.z);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f, %.2f, %.2f", key.rotation.x, key.rotation.y, key.rotation.z);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f, %.2f, %.2f", key.scale.x, key.scale.y, key.scale.z);
                }
                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Config")) {
            if (ImGui::DragFloat("Clip Length", &anim.clipLength, 0.05f, 0.1f, 120.0f, "%.2f")) {
                anim.clipLength = std::max(0.1f, anim.clipLength);
                projectManager.currentProject.hasUnsavedChanges = true;
                animationCurrentTime = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
            }
            if (ImGui::DragFloat("Play Speed", &anim.playSpeed, 0.05f, 0.05f, 8.0f, "%.2f")) {
                anim.playSpeed = std::max(0.05f, anim.playSpeed);
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::Checkbox("Loop", &anim.loop)) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::Checkbox("Apply On Scrub", &anim.applyOnScrub)) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Clear Keyframes")) {
                anim.keyframes.clear();
                animationSelectedKey = -1;
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Transport");
    ImGui::BeginDisabled(!anim.enabled);
    if (ImGui::Button(animationIsPlaying ? "Pause" : "Play")) {
        animationIsPlaying = !animationIsPlaying;
        animationLastAppliedTime = -1.0f;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        animationIsPlaying = false;
        animationCurrentTime = 0.0f;
        animationLastAppliedTime = -1.0f;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Time: %.2fs / %.2fs", animationCurrentTime, anim.clipLength);

    if (animationIsPlaying && anim.clipLength > 0.0f) {
        animationCurrentTime += ImGui::GetIO().DeltaTime * anim.playSpeed;
        if (animationCurrentTime > anim.clipLength) {
            if (anim.loop) {
                animationCurrentTime = std::fmod(animationCurrentTime, anim.clipLength);
            } else {
                animationCurrentTime = anim.clipLength;
                animationIsPlaying = false;
            }
        }
    }

    if (anim.enabled && (animationIsPlaying || anim.applyOnScrub)) {
        if (animationIsPlaying || std::abs(animationCurrentTime - animationLastAppliedTime) > 0.0001f) {
            applyPoseAtTime(*targetObj, animationCurrentTime);
            animationLastAppliedTime = animationCurrentTime;
        }
    }

    ImGui::EndChild();
    ImGui::EndTable();
    ImGui::End();
}
