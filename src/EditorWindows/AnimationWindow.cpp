#include "Engine.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

void Engine::renderAnimationWindow() {
    if (!showAnimationWindow) return;

    constexpr float kKeyTimeEpsilon = 0.0001f;

    auto clampFloat = [](float value, float minValue, float maxValue) {
        return std::max(minValue, std::min(value, maxValue));
    };

    auto lerpVec3 = [](const glm::vec3& a, const glm::vec3& b, float t) {
        return a + (b - a) * t;
    };

    auto copyStringToBuffer = [](const std::string& src, char* dst, size_t dstSize) {
        if (!dst || dstSize == 0) return;
        std::snprintf(dst, dstSize, "%s", src.c_str());
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
            case AnimationInterpolation::Step:
                return (t >= 1.0f) ? 1.0f : 0.0f;
            case AnimationInterpolation::Cubic:
                return t * t * t;
            case AnimationInterpolation::Linear:
            default:
                return t;
        }
    };

    const char* interpLabels[] = {
        "Linear", "SmoothStep", "Ease In", "Ease Out", "Ease In Out", "Step", "Cubic"
    };
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
            if (std::abs(dx) < kKeyTimeEpsilon) break;
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

    auto sortKeyframes = [](AnimationComponent& anim) {
        std::sort(anim.keyframes.begin(), anim.keyframes.end(),
                  [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; });
    };

    auto sortEvents = [](AnimationComponent& anim) {
        std::sort(anim.events.begin(), anim.events.end(),
                  [](const AnimationEvent& a, const AnimationEvent& b) { return a.time < b.time; });
    };

    auto findNearestKeyIndex = [&](const AnimationComponent& anim, float time, float tolerance) {
        int best = -1;
        float bestDist = FLT_MAX;
        for (size_t i = 0; i < anim.keyframes.size(); ++i) {
            float dist = std::abs(anim.keyframes[i].time - time);
            if (dist <= tolerance && dist < bestDist) {
                best = static_cast<int>(i);
                bestDist = dist;
            }
        }
        return best;
    };

    auto findNearestEventIndex = [&](const AnimationComponent& anim, float time, float tolerance) {
        int best = -1;
        float bestDist = FLT_MAX;
        for (size_t i = 0; i < anim.events.size(); ++i) {
            float dist = std::abs(anim.events[i].time - time);
            if (dist <= tolerance && dist < bestDist) {
                best = static_cast<int>(i);
                bestDist = dist;
            }
        }
        return best;
    };

    auto captureKeyframe = [&](SceneObject& obj) {
        auto& anim = obj.animation;
        float clamped = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
        int existing = findNearestKeyIndex(anim, clamped, kKeyTimeEpsilon);
        if (existing < 0) {
            AnimationKeyframe key;
            key.time = clamped;
            key.position = obj.position;
            key.rotation = obj.rotation;
            key.scale = obj.scale;
            key.interpolation = AnimationInterpolation::SmoothStep;
            key.curveMode = AnimationCurveMode::Preset;
            anim.keyframes.push_back(key);
        } else {
            auto& key = anim.keyframes[existing];
            key.position = obj.position;
            key.rotation = obj.rotation;
            key.scale = obj.scale;
        }
        sortKeyframes(anim);
        projectManager.currentProject.hasUnsavedChanges = true;
    };

    auto deleteKeyframeByIndex = [&](SceneObject& obj, int index) {
        auto& anim = obj.animation;
        if (index < 0 || index >= static_cast<int>(anim.keyframes.size())) return false;
        anim.keyframes.erase(anim.keyframes.begin() + index);
        projectManager.currentProject.hasUnsavedChanges = true;
        return true;
    };

    auto deleteKeyframeNearTime = [&](SceneObject& obj, float time) {
        auto& anim = obj.animation;
        int idx = findNearestKeyIndex(anim, time, 0.01f);
        if (idx < 0) return false;
        anim.keyframes.erase(anim.keyframes.begin() + idx);
        projectManager.currentProject.hasUnsavedChanges = true;
        return true;
    };

    auto moveMatchingKeyToTime = [&](SceneObject& obj, float sourceTime, float destinationTime) {
        auto& anim = obj.animation;
        int idx = findNearestKeyIndex(anim, sourceTime, 0.01f);
        if (idx < 0) return false;
        anim.keyframes[idx].time = clampFloat(destinationTime, 0.0f, anim.clipLength);
        sortKeyframes(anim);
        projectManager.currentProject.hasUnsavedChanges = true;
        return true;
    };

    auto addEventAtCurrentTime = [&](SceneObject& obj) {
        auto& anim = obj.animation;
        AnimationEvent evt;
        evt.time = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
        evt.eventId = "AnimEvent";
        anim.events.push_back(evt);
        sortEvents(anim);
        animationSelectedEvent = findNearestEventIndex(anim, evt.time, 0.001f);
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

    auto dispatchAnimationEvents = [&](const SceneObject& obj, float previousTime, float currentTime, bool wrapped) {
        if (!animationLogEvents || obj.animation.events.empty()) return;
        auto fireEvent = [&](const AnimationEvent& evt) {
            std::string msg = "Animation Event [" + obj.name + "] " + evt.eventId;
            if (!evt.payload.empty()) {
                msg += " payload=" + evt.payload;
            }
            addConsoleMessage(msg, ConsoleMessageType::Info);
        };

        if (previousTime < 0.0f) {
            for (const auto& evt : obj.animation.events) {
                if (std::abs(evt.time - currentTime) <= 0.001f) {
                    fireEvent(evt);
                }
            }
            return;
        }

        if (wrapped) {
            for (const auto& evt : obj.animation.events) {
                if ((evt.time > previousTime && evt.time <= obj.animation.clipLength) ||
                    (evt.time >= 0.0f && evt.time <= currentTime)) {
                    fireEvent(evt);
                }
            }
            return;
        }

        if (currentTime >= previousTime) {
            for (const auto& evt : obj.animation.events) {
                if (evt.time > previousTime && evt.time <= currentTime) {
                    fireEvent(evt);
                }
            }
        } else {
            for (const auto& evt : obj.animation.events) {
                if (evt.time >= currentTime && evt.time < previousTime) {
                    fireEvent(evt);
                }
            }
        }
    };

    auto sortPropertyKeys = [](AnimationPropertyTrack& track) {
        std::sort(track.keyframes.begin(), track.keyframes.end(),
                  [](const AnimationPropertyKeyframe& a, const AnimationPropertyKeyframe& b) { return a.time < b.time; });
    };

    auto findNearestPropertyKeyIndex = [&](const AnimationPropertyTrack& track, float time, float tolerance) {
        int best = -1;
        float bestDist = FLT_MAX;
        for (size_t i = 0; i < track.keyframes.size(); ++i) {
            float dist = std::abs(track.keyframes[i].time - time);
            if (dist <= tolerance && dist < bestDist) {
                best = static_cast<int>(i);
                bestDist = dist;
            }
        }
        return best;
    };

    auto tryParseFloatString = [](const std::string& value, float& outValue) {
        const char* begin = value.c_str();
        char* end = nullptr;
        outValue = std::strtof(begin, &end);
        if (end == begin) return false;
        while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        return *end == '\0';
    };

    auto getAnimFloatPropertyValue = [&](const SceneObject& obj, const std::string& path, float& outValue) -> bool {
        if (path == "Transform.Position.x") { outValue = obj.position.x; return true; }
        if (path == "Transform.Position.y") { outValue = obj.position.y; return true; }
        if (path == "Transform.Position.z") { outValue = obj.position.z; return true; }
        if (path == "Transform.Rotation.x") { outValue = obj.rotation.x; return true; }
        if (path == "Transform.Rotation.y") { outValue = obj.rotation.y; return true; }
        if (path == "Transform.Rotation.z") { outValue = obj.rotation.z; return true; }
        if (path == "Transform.Scale.x") { outValue = obj.scale.x; return true; }
        if (path == "Transform.Scale.y") { outValue = obj.scale.y; return true; }
        if (path == "Transform.Scale.z") { outValue = obj.scale.z; return true; }

        if (path == "Light.Intensity" && obj.hasLight) { outValue = obj.light.intensity; return true; }
        if (path == "Light.Range" && obj.hasLight) { outValue = obj.light.range; return true; }
        if (path == "Light.InnerAngle" && obj.hasLight) { outValue = obj.light.innerAngle; return true; }
        if (path == "Light.OuterAngle" && obj.hasLight) { outValue = obj.light.outerAngle; return true; }
        if (path == "Light.Enabled" && obj.hasLight) { outValue = obj.light.enabled ? 1.0f : 0.0f; return true; }

        if (path == "Camera.FOV" && obj.hasCamera) { outValue = obj.camera.fov; return true; }
        if (path == "Camera.NearClip" && obj.hasCamera) { outValue = obj.camera.nearClip; return true; }
        if (path == "Camera.FarClip" && obj.hasCamera) { outValue = obj.camera.farClip; return true; }
        if (path == "Camera.PixelsPerUnit" && obj.hasCamera) { outValue = obj.camera.pixelsPerUnit; return true; }

        if (path == "PostFX.BloomIntensity" && obj.hasPostFX) { outValue = obj.postFx.bloomIntensity; return true; }
        if (path == "PostFX.Exposure" && obj.hasPostFX) { outValue = obj.postFx.exposure; return true; }
        if (path == "PostFX.Contrast" && obj.hasPostFX) { outValue = obj.postFx.contrast; return true; }
        if (path == "PostFX.Saturation" && obj.hasPostFX) { outValue = obj.postFx.saturation; return true; }

        if (path == "Rigidbody.Mass" && obj.hasRigidbody) { outValue = obj.rigidbody.mass; return true; }

        if (path == "Audio.Volume" && obj.hasAudioSource) { outValue = obj.audioSource.volume; return true; }
        if (path == "Audio.MinDistance" && obj.hasAudioSource) { outValue = obj.audioSource.minDistance; return true; }
        if (path == "Audio.MaxDistance" && obj.hasAudioSource) { outValue = obj.audioSource.maxDistance; return true; }
        if (path == "Audio.Loop" && obj.hasAudioSource) { outValue = obj.audioSource.loop ? 1.0f : 0.0f; return true; }
        if (path == "Audio.Spatial" && obj.hasAudioSource) { outValue = obj.audioSource.spatial ? 1.0f : 0.0f; return true; }

        if (path == "AIAgent.Speed" && obj.hasAIAgent) { outValue = obj.aiAgent.speed; return true; }
        if (path == "AIAgent.StoppingDistance" && obj.hasAIAgent) { outValue = obj.aiAgent.stoppingDistance; return true; }

        int scriptIndex = -1;
        int settingIndex = -1;
        if (std::sscanf(path.c_str(), "ScriptSetting.%d.%d", &scriptIndex, &settingIndex) == 2) {
            if (scriptIndex >= 0 && scriptIndex < static_cast<int>(obj.scripts.size())) {
                const auto& script = obj.scripts[scriptIndex];
                if (settingIndex >= 0 && settingIndex < static_cast<int>(script.settings.size())) {
                    return tryParseFloatString(script.settings[settingIndex].value, outValue);
                }
            }
        }

        return false;
    };

    auto setAnimFloatPropertyValue = [&](SceneObject& obj, const std::string& path, float value, bool* outTransformDirty) -> bool {
        auto markTransform = [&]() {
            if (outTransformDirty) *outTransformDirty = true;
        };

        if (path == "Transform.Position.x") { obj.position.x = value; markTransform(); return true; }
        if (path == "Transform.Position.y") { obj.position.y = value; markTransform(); return true; }
        if (path == "Transform.Position.z") { obj.position.z = value; markTransform(); return true; }
        if (path == "Transform.Rotation.x") { obj.rotation.x = value; markTransform(); return true; }
        if (path == "Transform.Rotation.y") { obj.rotation.y = value; markTransform(); return true; }
        if (path == "Transform.Rotation.z") { obj.rotation.z = value; markTransform(); return true; }
        if (path == "Transform.Scale.x") { obj.scale.x = std::max(0.001f, value); markTransform(); return true; }
        if (path == "Transform.Scale.y") { obj.scale.y = std::max(0.001f, value); markTransform(); return true; }
        if (path == "Transform.Scale.z") { obj.scale.z = std::max(0.001f, value); markTransform(); return true; }

        if (path == "Light.Intensity" && obj.hasLight) { obj.light.intensity = value; return true; }
        if (path == "Light.Range" && obj.hasLight) { obj.light.range = std::max(0.0f, value); return true; }
        if (path == "Light.InnerAngle" && obj.hasLight) { obj.light.innerAngle = std::clamp(value, 0.0f, 180.0f); return true; }
        if (path == "Light.OuterAngle" && obj.hasLight) { obj.light.outerAngle = std::clamp(value, 0.0f, 180.0f); return true; }
        if (path == "Light.Enabled" && obj.hasLight) { obj.light.enabled = value >= 0.5f; return true; }

        if (path == "Camera.FOV" && obj.hasCamera) { obj.camera.fov = std::clamp(value, 1.0f, 179.0f); return true; }
        if (path == "Camera.NearClip" && obj.hasCamera) { obj.camera.nearClip = std::max(0.001f, value); return true; }
        if (path == "Camera.FarClip" && obj.hasCamera) { obj.camera.farClip = std::max(obj.camera.nearClip + 0.01f, value); return true; }
        if (path == "Camera.PixelsPerUnit" && obj.hasCamera) { obj.camera.pixelsPerUnit = std::max(1.0f, value); return true; }

        if (path == "PostFX.BloomIntensity" && obj.hasPostFX) { obj.postFx.bloomIntensity = std::max(0.0f, value); return true; }
        if (path == "PostFX.Exposure" && obj.hasPostFX) { obj.postFx.exposure = value; return true; }
        if (path == "PostFX.Contrast" && obj.hasPostFX) { obj.postFx.contrast = std::max(0.0f, value); return true; }
        if (path == "PostFX.Saturation" && obj.hasPostFX) { obj.postFx.saturation = std::max(0.0f, value); return true; }

        if (path == "Rigidbody.Mass" && obj.hasRigidbody) { obj.rigidbody.mass = std::max(0.001f, value); return true; }

        if (path == "Audio.Volume" && obj.hasAudioSource) { obj.audioSource.volume = std::max(0.0f, value); return true; }
        if (path == "Audio.MinDistance" && obj.hasAudioSource) { obj.audioSource.minDistance = std::max(0.01f, value); return true; }
        if (path == "Audio.MaxDistance" && obj.hasAudioSource) { obj.audioSource.maxDistance = std::max(obj.audioSource.minDistance + 0.01f, value); return true; }
        if (path == "Audio.Loop" && obj.hasAudioSource) { obj.audioSource.loop = value >= 0.5f; return true; }
        if (path == "Audio.Spatial" && obj.hasAudioSource) { obj.audioSource.spatial = value >= 0.5f; return true; }

        if (path == "AIAgent.Speed" && obj.hasAIAgent) { obj.aiAgent.speed = std::max(0.05f, value); return true; }
        if (path == "AIAgent.StoppingDistance" && obj.hasAIAgent) { obj.aiAgent.stoppingDistance = std::max(0.0f, value); return true; }

        int scriptIndex = -1;
        int settingIndex = -1;
        if (std::sscanf(path.c_str(), "ScriptSetting.%d.%d", &scriptIndex, &settingIndex) == 2) {
            if (scriptIndex >= 0 && scriptIndex < static_cast<int>(obj.scripts.size())) {
                auto& script = obj.scripts[scriptIndex];
                if (settingIndex >= 0 && settingIndex < static_cast<int>(script.settings.size())) {
                    char numeric[64];
                    std::snprintf(numeric, sizeof(numeric), "%.6g", value);
                    script.settings[settingIndex].value = numeric;
                    return true;
                }
            }
        }

        return false;
    };

    auto samplePropertyTrackValue = [&](const AnimationPropertyTrack& track, float time) {
        if (track.keyframes.empty()) {
            return track.defaultValue;
        }
        if (time <= track.keyframes.front().time) {
            return track.keyframes.front().value;
        }
        if (time >= track.keyframes.back().time) {
            return track.keyframes.back().value;
        }
        for (size_t i = 0; i + 1 < track.keyframes.size(); ++i) {
            const auto& a = track.keyframes[i];
            const auto& b = track.keyframes[i + 1];
            if (time >= a.time && time <= b.time) {
                float span = b.time - a.time;
                float t = (span > 0.0f) ? (time - a.time) / span : 0.0f;
                if (a.curveMode == AnimationCurveMode::Bezier) {
                    t = applyBezier(t, a.bezierOut, b.bezierIn);
                } else {
                    t = applyInterpolation(t, a.interpolation);
                }
                return a.value + (b.value - a.value) * t;
            }
        }
        return track.keyframes.back().value;
    };

    auto upsertPropertyKey = [&](AnimationPropertyTrack& track, float time, float value) {
        int existing = findNearestPropertyKeyIndex(track, time, 0.001f);
        if (existing >= 0 && existing < static_cast<int>(track.keyframes.size())) {
            track.keyframes[existing].time = time;
            track.keyframes[existing].value = value;
        } else {
            AnimationPropertyKeyframe key;
            key.time = time;
            key.value = value;
            track.keyframes.push_back(key);
        }
        sortPropertyKeys(track);
    };

    auto applyPropertyTracksAtTime = [&](SceneObject& obj, float time) {
        if (!obj.hasAnimation) return;
        bool transformDirty = false;
        for (const auto& track : obj.animation.tracks) {
            if (!track.enabled || track.path.empty()) continue;
            float sampled = samplePropertyTrackValue(track, time);
            setAnimFloatPropertyValue(obj, track.path, sampled, &transformDirty);
        }
        if (transformDirty) {
            syncLocalTransform(obj);
        }
    };

    struct AnimPropertyCandidate {
        std::string path;
        std::string label;
    };
    auto buildAnimPropertyCandidates = [&](SceneObject& obj) {
        std::vector<AnimPropertyCandidate> out;
        auto push = [&](const std::string& path, const std::string& label) {
            out.push_back({path, label});
        };

        push("Transform.Position.x", "Transform/Position.x");
        push("Transform.Position.y", "Transform/Position.y");
        push("Transform.Position.z", "Transform/Position.z");
        push("Transform.Rotation.x", "Transform/Rotation.x");
        push("Transform.Rotation.y", "Transform/Rotation.y");
        push("Transform.Rotation.z", "Transform/Rotation.z");
        push("Transform.Scale.x", "Transform/Scale.x");
        push("Transform.Scale.y", "Transform/Scale.y");
        push("Transform.Scale.z", "Transform/Scale.z");

        if (obj.hasLight) {
            push("Light.Intensity", "Light/Intensity");
            push("Light.Range", "Light/Range");
            push("Light.InnerAngle", "Light/Inner Angle");
            push("Light.OuterAngle", "Light/Outer Angle");
            push("Light.Enabled", "Light/Enabled");
        }
        if (obj.hasCamera) {
            push("Camera.FOV", "Camera/FOV");
            push("Camera.NearClip", "Camera/Near Clip");
            push("Camera.FarClip", "Camera/Far Clip");
            push("Camera.PixelsPerUnit", "Camera/Pixels Per Unit");
        }
        if (obj.hasPostFX) {
            push("PostFX.BloomIntensity", "PostFX/Bloom Intensity");
            push("PostFX.Exposure", "PostFX/Exposure");
            push("PostFX.Contrast", "PostFX/Contrast");
            push("PostFX.Saturation", "PostFX/Saturation");
        }
        if (obj.hasRigidbody) {
            push("Rigidbody.Mass", "Rigidbody/Mass");
        }
        if (obj.hasAudioSource) {
            push("Audio.Volume", "Audio/Volume");
            push("Audio.MinDistance", "Audio/Min Distance");
            push("Audio.MaxDistance", "Audio/Max Distance");
            push("Audio.Loop", "Audio/Loop");
            push("Audio.Spatial", "Audio/Spatial");
        }
        if (obj.hasAIAgent) {
            push("AIAgent.Speed", "AI Agent/Speed");
            push("AIAgent.StoppingDistance", "AI Agent/Stopping Distance");
        }
        for (size_t si = 0; si < obj.scripts.size(); ++si) {
            const auto& script = obj.scripts[si];
            for (size_t st = 0; st < script.settings.size(); ++st) {
                float numericValue = 0.0f;
                if (!tryParseFloatString(script.settings[st].value, numericValue)) {
                    continue;
                }
                std::string settingName = script.settings[st].key.empty()
                    ? ("Setting " + std::to_string(st))
                    : script.settings[st].key;
                std::string path = "ScriptSetting." + std::to_string(si) + "." + std::to_string(st);
                std::string label = "Script/" + std::to_string(si) + "/" + settingName;
                push(path, label);
            }
        }
        return out;
    };

    auto* selectedObj = getSelectedObject();
    std::vector<SceneObject*> animTargets;
    animTargets.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        if (obj.hasAnimation) animTargets.push_back(&obj);
    }

    auto resolveTarget = [&](int id) -> SceneObject* {
        if (id < 0) return nullptr;
        SceneObject* obj = findObjectById(id);
        if (!obj || !obj->hasAnimation) return nullptr;
        return obj;
    };

    SceneObject* targetObj = resolveTarget(animationTargetId);
    if (!targetObj && !animTargets.empty()) {
        animationTargetId = animTargets.front()->id;
        animationSelectedKey = -1;
        animationSelectedEvent = -1;
        animationLastAppliedTime = -1.0f;
        targetObj = resolveTarget(animationTargetId);
    }

    {
        std::unordered_set<int> seen;
        std::vector<int> deduped;
        deduped.reserve(animationEditTargetIds.size());
        for (int id : animationEditTargetIds) {
            if (!resolveTarget(id)) continue;
            if (seen.insert(id).second) deduped.push_back(id);
        }
        animationEditTargetIds.swap(deduped);
    }

    if (targetObj) {
        auto it = std::find(animationEditTargetIds.begin(), animationEditTargetIds.end(), targetObj->id);
        if (it == animationEditTargetIds.end()) {
            animationEditTargetIds.push_back(targetObj->id);
        }
    }

    std::vector<SceneObject*> editTargets;
    if (animationApplyToSelection) {
        for (int id : animationEditTargetIds) {
            if (SceneObject* obj = resolveTarget(id)) editTargets.push_back(obj);
        }
    }
    if (targetObj) {
        auto it = std::find(editTargets.begin(), editTargets.end(), targetObj);
        if (it == editTargets.end()) editTargets.insert(editTargets.begin(), targetObj);
    }
    if (editTargets.empty() && targetObj) {
        editTargets.push_back(targetObj);
    }

    struct TimelineDragState {
        int targetId = -1;
        int mode = 0; // 0 = none, 1 = single key, 2 = grouped marker
        int keyIndex = -1;
        float sourceTime = 0.0f;
        bool moved = false;
    };
    static TimelineDragState dragState;

    auto drawTimeline = [&](SceneObject& primaryObj, const std::vector<SceneObject*>& overlayTargets) {
        auto& anim = primaryObj.animation;
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 96.0f);
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("AnimationTimeline", size);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
        ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_SliderGrab);
        ImU32 tickColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ImU32 eventColor = ImGui::GetColorU32(ImVec4(0.95f, 0.75f, 0.28f, 1.0f));

        draw->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), bg, 6.0f);
        draw->AddRect(start, ImVec2(start.x + size.x, start.y + size.y), border, 6.0f);

        if (anim.clipLength > 0.0f) {
            int majorTicks = std::max(2, static_cast<int>(std::ceil(anim.clipLength)));
            for (int i = 0; i <= majorTicks; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(majorTicks);
                float x = start.x + t * size.x;
                float timeValue = t * anim.clipLength;
                draw->AddLine(ImVec2(x, start.y + 2.0f), ImVec2(x, start.y + size.y - 18.0f), tickColor, 1.0f);
                char tickLabel[16];
                std::snprintf(tickLabel, sizeof(tickLabel), "%.1f", timeValue);
                draw->AddText(ImVec2(x + 2.0f, start.y + size.y - 16.0f), tickColor, tickLabel);
            }
        }

        float clampedTime = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
        float playheadX = start.x + (anim.clipLength > 0.0f ? (clampedTime / anim.clipLength) * size.x : 0.0f);
        draw->AddLine(ImVec2(playheadX, start.y), ImVec2(playheadX, start.y + size.y), accent, 2.0f);

        int hoveredMarkerBucket = -1;
        if (animationApplyToSelection && overlayTargets.size() > 1) {
            std::unordered_map<int, int> markerCounts;
            for (auto* obj : overlayTargets) {
                if (!obj) continue;
                for (const auto& key : obj->animation.keyframes) {
                    int bucket = static_cast<int>(std::lround(key.time * 1000.0f));
                    markerCounts[bucket] += 1;
                }
            }

            for (const auto& entry : markerCounts) {
                float keyTime = static_cast<float>(entry.first) / 1000.0f;
                float keyX = start.x + (anim.clipLength > 0.0f ? (keyTime / anim.clipLength) * size.x : 0.0f);
                draw->AddCircleFilled(ImVec2(keyX, start.y + 11.0f), 2.5f, tickColor);
                ImRect markerHit(ImVec2(keyX - 6.0f, start.y + 5.0f), ImVec2(keyX + 6.0f, start.y + 17.0f));
                if (ImGui::IsMouseHoveringRect(markerHit.Min, markerHit.Max)) {
                    hoveredMarkerBucket = entry.first;
                }
                if (entry.second > 1) {
                    char countLabel[12];
                    std::snprintf(countLabel, sizeof(countLabel), "%d", entry.second);
                    draw->AddText(ImVec2(keyX + 3.0f, start.y + 3.0f), tickColor, countLabel);
                }
            }
        }

        int hoveredEvent = -1;
        for (size_t i = 0; i < anim.events.size(); ++i) {
            float eventX = start.x + (anim.clipLength > 0.0f ? (anim.events[i].time / anim.clipLength) * size.x : 0.0f);
            ImVec2 a(eventX, start.y + size.y - 21.0f);
            ImVec2 b(eventX - 4.0f, start.y + size.y - 13.0f);
            ImVec2 c(eventX + 4.0f, start.y + size.y - 13.0f);
            ImU32 col = (animationSelectedEvent == static_cast<int>(i)) ? accent : eventColor;
            draw->AddTriangleFilled(a, b, c, col);
            ImRect hit(ImVec2(eventX - 6.0f, start.y + size.y - 23.0f), ImVec2(eventX + 6.0f, start.y + size.y - 10.0f));
            if (ImGui::IsMouseHoveringRect(hit.Min, hit.Max)) hoveredEvent = static_cast<int>(i);
        }

        int hoveredKey = -1;
        for (size_t i = 0; i < anim.keyframes.size(); ++i) {
            float keyX = start.x +
                (anim.clipLength > 0.0f ? (anim.keyframes[i].time / anim.clipLength) * size.x : 0.0f);
            ImVec2 center(keyX, start.y + (size.y - 18.0f) * 0.5f);
            float radius = (animationSelectedKey == static_cast<int>(i)) ? 6.0f : 4.5f;
            draw->AddCircleFilled(center, radius, keyColor);

            ImRect hit(ImVec2(center.x - 7.0f, center.y - 7.0f), ImVec2(center.x + 7.0f, center.y + 7.0f));
            if (ImGui::IsMouseHoveringRect(hit.Min, hit.Max)) {
                hoveredKey = static_cast<int>(i);
            }
        }

        bool itemHovered = ImGui::IsItemHovered();
        if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredKey >= 0) {
                animationSelectedKey = hoveredKey;
                animationSelectedEvent = -1;
                animationCurrentTime = anim.keyframes[hoveredKey].time;
                dragState.targetId = primaryObj.id;
                dragState.mode = 1;
                dragState.keyIndex = hoveredKey;
                dragState.sourceTime = anim.keyframes[hoveredKey].time;
                dragState.moved = false;
            } else if (hoveredEvent >= 0) {
                animationSelectedEvent = hoveredEvent;
                animationSelectedKey = -1;
                animationCurrentTime = anim.events[hoveredEvent].time;
            } else if (hoveredMarkerBucket >= 0) {
                float markerTime = static_cast<float>(hoveredMarkerBucket) / 1000.0f;
                animationCurrentTime = clampFloat(markerTime, 0.0f, anim.clipLength);
                animationSelectedEvent = -1;
                animationSelectedKey = findNearestKeyIndex(anim, animationCurrentTime, 0.02f);
                dragState.targetId = primaryObj.id;
                dragState.mode = 2;
                dragState.keyIndex = -1;
                dragState.sourceTime = animationCurrentTime;
                dragState.moved = false;
            } else {
                float mouseX = ImGui::GetIO().MousePos.x;
                float t = (mouseX - start.x) / size.x;
                animationCurrentTime = clampFloat(t * anim.clipLength, 0.0f, anim.clipLength);
                animationSelectedKey = -1;
            }
        }

        if (dragState.targetId == primaryObj.id && dragState.mode == 1 && dragState.keyIndex >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (dragState.keyIndex < static_cast<int>(anim.keyframes.size())) {
                    float mouseX = ImGui::GetIO().MousePos.x;
                    float t = (mouseX - start.x) / size.x;
                    float draggedTime = clampFloat(t * anim.clipLength, 0.0f, anim.clipLength);
                    if (ImGui::GetIO().KeyShift && anim.clipLength > 0.0f) {
                        float snapStep = 0.05f;
                        draggedTime = std::round(draggedTime / snapStep) * snapStep;
                        draggedTime = clampFloat(draggedTime, 0.0f, anim.clipLength);
                    }
                    anim.keyframes[dragState.keyIndex].time = draggedTime;
                    animationCurrentTime = draggedTime;
                    dragState.moved = true;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            } else {
                if (dragState.keyIndex < static_cast<int>(anim.keyframes.size())) {
                    float oldTime = dragState.sourceTime;
                    float newTime = clampFloat(anim.keyframes[dragState.keyIndex].time, 0.0f, anim.clipLength);
                    sortKeyframes(anim);
                    animationSelectedKey = findNearestKeyIndex(anim, newTime, 0.02f);
                    animationCurrentTime = newTime;

                    if (dragState.moved && animationApplyToSelection && overlayTargets.size() > 1) {
                        for (auto* obj : overlayTargets) {
                            if (!obj || obj->id == primaryObj.id) continue;
                            moveMatchingKeyToTime(*obj, oldTime, newTime);
                        }
                    }
                }
                dragState = TimelineDragState{};
            }
        }

        if (dragState.targetId == primaryObj.id && dragState.mode == 2) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float mouseX = ImGui::GetIO().MousePos.x;
                float t = (mouseX - start.x) / size.x;
                float draggedTime = clampFloat(t * anim.clipLength, 0.0f, anim.clipLength);
                if (ImGui::GetIO().KeyShift && anim.clipLength > 0.0f) {
                    float snapStep = 0.05f;
                    draggedTime = std::round(draggedTime / snapStep) * snapStep;
                    draggedTime = clampFloat(draggedTime, 0.0f, anim.clipLength);
                }
                animationCurrentTime = draggedTime;
                dragState.moved = true;
            } else {
                float oldTime = dragState.sourceTime;
                float newTime = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
                if (dragState.moved && animationApplyToSelection && overlayTargets.size() > 1) {
                    for (auto* obj : overlayTargets) {
                        if (!obj) continue;
                        moveMatchingKeyToTime(*obj, oldTime, newTime);
                    }
                }
                animationSelectedKey = findNearestKeyIndex(anim, newTime, 0.02f);
                animationCurrentTime = newTime;
                dragState = TimelineDragState{};
            }
        }

        if (itemHovered &&
            dragState.keyIndex < 0 &&
            hoveredKey < 0 &&
            hoveredEvent < 0 &&
            hoveredMarkerBucket < 0 &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float t = (mouseX - start.x) / size.x;
            animationCurrentTime = clampFloat(t * anim.clipLength, 0.0f, anim.clipLength);
        }
    };

    ImGui::Begin("Animation", &showAnimationWindow, ImGuiWindowFlags_NoCollapse);

    if (!ImGui::BeginTable("AnimatorLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::End();
        return;
    }

    ImGui::TableSetupColumn("Targets", ImGuiTableColumnFlags_WidthFixed, 240.0f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::BeginChild("AnimatorTargets", ImVec2(0, 0), true);
    ImGui::TextDisabled("Targets (Ctrl+Click for multi)");
    ImGui::Spacing();

    static char targetFilter[96] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##AnimTargetFilter", "Filter targets...", targetFilter, sizeof(targetFilter));
    std::string targetFilterLower = targetFilter;
    std::transform(targetFilterLower.begin(), targetFilterLower.end(), targetFilterLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    ImGui::Spacing();
    ImGui::BeginDisabled(!selectedObj);
    if (ImGui::Button("Add Animation to Selected", ImVec2(-1, 0))) {
        if (selectedObj && !selectedObj->hasAnimation) {
            selectedObj->hasAnimation = true;
            selectedObj->animation = AnimationComponent{};
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        if (selectedObj && selectedObj->hasAnimation) {
            animationTargetId = selectedObj->id;
            animationSelectedKey = -1;
            animationSelectedEvent = -1;
            animationLastAppliedTime = -1.0f;
            animationCurrentTime = 0.0f;
            if (std::find(animationEditTargetIds.begin(), animationEditTargetIds.end(), selectedObj->id) ==
                animationEditTargetIds.end()) {
                animationEditTargetIds.push_back(selectedObj->id);
            }
            targetObj = resolveTarget(animationTargetId);
        }
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Use Scene Selection", ImVec2(-1, 0))) {
        animationEditTargetIds.clear();
        for (int id : selectedObjectIds) {
            if (resolveTarget(id)) animationEditTargetIds.push_back(id);
        }
        if (!animationEditTargetIds.empty()) {
            animationTargetId = animationEditTargetIds.back();
            animationSelectedKey = -1;
            animationSelectedEvent = -1;
            animationLastAppliedTime = -1.0f;
            animationCurrentTime = 0.0f;
            targetObj = resolveTarget(animationTargetId);
        }
    }

    ImGui::BeginDisabled(!targetObj);
    if (ImGui::Button("Keep Primary Only", ImVec2(-1, 0)) && targetObj) {
        animationEditTargetIds.clear();
        animationEditTargetIds.push_back(targetObj->id);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (animTargets.empty()) {
        ImGui::TextDisabled("No Animation components yet.");
    } else {
        int visibleTargets = 0;
        for (auto* obj : animTargets) {
            std::string objNameLower = obj->name;
            std::transform(objNameLower.begin(), objNameLower.end(), objNameLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!targetFilterLower.empty() && objNameLower.find(targetFilterLower) == std::string::npos) {
                continue;
            }
            visibleTargets++;

            bool inMultiSelection =
                std::find(animationEditTargetIds.begin(), animationEditTargetIds.end(), obj->id) != animationEditTargetIds.end();
            bool isPrimary = (targetObj && targetObj->id == obj->id);
            std::string label = isPrimary ? "[P] " + obj->name : obj->name;

            ImGui::PushID(obj->id);
            if (ImGui::Selectable(label.c_str(), inMultiSelection)) {
                bool additive = ImGui::GetIO().KeyCtrl;
                if (!additive) {
                    animationEditTargetIds.clear();
                    animationEditTargetIds.push_back(obj->id);
                } else {
                    auto it = std::find(animationEditTargetIds.begin(), animationEditTargetIds.end(), obj->id);
                    if (it == animationEditTargetIds.end()) {
                        animationEditTargetIds.push_back(obj->id);
                    } else if (animationEditTargetIds.size() > 1) {
                        animationEditTargetIds.erase(it);
                    }
                }

                animationTargetId = obj->id;
                animationSelectedKey = -1;
                animationSelectedEvent = -1;
                animationLastAppliedTime = -1.0f;
                animationCurrentTime = 0.0f;
                targetObj = obj;
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Keyframes: %zu", obj->animation.keyframes.size());
                ImGui::Text("Events: %zu", obj->animation.events.size());
                ImGui::Text("Length: %.2fs", obj->animation.clipLength);
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        if (visibleTargets == 0) {
            ImGui::TextDisabled("No targets match filter.");
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
    ImGui::TextDisabled("Target: %s  |  Keyframes: %zu  |  Events: %zu",
                        targetObj->name.c_str(),
                        anim.keyframes.size(),
                        anim.events.size());

    ImGui::Checkbox("Apply edits to selected targets", &animationApplyToSelection);
    ImGui::SameLine();
    ImGui::Checkbox("Log events", &animationLogEvents);
    ImGui::SameLine();
    ImGui::Checkbox("Record", &animationRecordMode);
    ImGui::SameLine();
    ImGui::TextDisabled("Edit targets: %zu", editTargets.size());

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginTabBar("AnimatorTabs")) {
        if (ImGui::BeginTabItem("Pose")) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::BeginDisabled(!anim.enabled);
            if (ImGui::Button("Capture Key")) {
                for (auto* obj : editTargets) {
                    if (obj && obj->animation.enabled) captureKeyframe(*obj);
                }
                animationSelectedKey = findNearestKeyIndex(anim, animationCurrentTime, 0.01f);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(anim.keyframes.empty());
            if (ImGui::Button("Delete")) {
                float deleteTime = animationCurrentTime;
                if (animationSelectedKey >= 0 && animationSelectedKey < static_cast<int>(anim.keyframes.size())) {
                    deleteTime = anim.keyframes[animationSelectedKey].time;
                    deleteKeyframeByIndex(*targetObj, animationSelectedKey);
                } else {
                    deleteKeyframeNearTime(*targetObj, deleteTime);
                }
                for (auto* obj : editTargets) {
                    if (!obj || obj->id == targetObj->id) continue;
                    deleteKeyframeNearTime(*obj, deleteTime);
                }
                animationSelectedKey = -1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(anim.keyframes.empty());
            if (ImGui::Button("Sort")) {
                for (auto* obj : editTargets) {
                    if (!obj) continue;
                    sortKeyframes(obj->animation);
                }
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(anim.keyframes.empty());
            if (ImGui::Button("Prev Key")) {
                int best = -1;
                float bestTime = -1.0f;
                for (size_t i = 0; i < anim.keyframes.size(); ++i) {
                    if (anim.keyframes[i].time < animationCurrentTime &&
                        (best < 0 || anim.keyframes[i].time > bestTime)) {
                        best = static_cast<int>(i);
                        bestTime = anim.keyframes[i].time;
                    }
                }
                if (best < 0) best = 0;
                animationSelectedKey = best;
                animationSelectedEvent = -1;
                animationCurrentTime = anim.keyframes[best].time;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(anim.keyframes.empty());
            if (ImGui::Button("Next Key")) {
                int best = -1;
                float bestTime = FLT_MAX;
                for (size_t i = 0; i < anim.keyframes.size(); ++i) {
                    if (anim.keyframes[i].time > animationCurrentTime &&
                        (best < 0 || anim.keyframes[i].time < bestTime)) {
                        best = static_cast<int>(i);
                        bestTime = anim.keyframes[i].time;
                    }
                }
                if (best < 0) best = static_cast<int>(anim.keyframes.size()) - 1;
                animationSelectedKey = best;
                animationSelectedEvent = -1;
                animationCurrentTime = anim.keyframes[best].time;
            }
            ImGui::EndDisabled();
            ImGui::PopStyleVar();

            ImGui::Spacing();
            ImGui::SeparatorText("Timeline");
            drawTimeline(*targetObj, editTargets);
            ImGui::SliderFloat("Time", &animationCurrentTime, 0.0f, anim.clipLength, "%.2fs");

            if (animationSelectedKey >= 0 && animationSelectedKey < static_cast<int>(anim.keyframes.size())) {
                AnimationKeyframe* selectedKey = &anim.keyframes[animationSelectedKey];
                ImGui::Separator();
                ImGui::TextDisabled("Selected Key");

                float oldSelectedTime = selectedKey->time;
                if (ImGui::DragFloat("Key Time", &selectedKey->time, 0.01f, 0.0f, anim.clipLength, "%.2f")) {
                    selectedKey->time = clampFloat(selectedKey->time, 0.0f, anim.clipLength);
                    float newSelectedTime = selectedKey->time;
                    sortKeyframes(anim);
                    animationSelectedKey = findNearestKeyIndex(anim, newSelectedTime, 0.02f);
                    animationCurrentTime = newSelectedTime;
                    if (animationApplyToSelection && editTargets.size() > 1) {
                        for (auto* obj : editTargets) {
                            if (!obj || obj->id == targetObj->id) continue;
                            moveMatchingKeyToTime(*obj, oldSelectedTime, newSelectedTime);
                        }
                    }
                    projectManager.currentProject.hasUnsavedChanges = true;
                    if (animationSelectedKey >= 0 && animationSelectedKey < static_cast<int>(anim.keyframes.size())) {
                        selectedKey = &anim.keyframes[animationSelectedKey];
                    } else {
                        selectedKey = nullptr;
                    }
                }

                if (selectedKey) {
                    auto applyToMatchingKeys = [&](const std::function<void(AnimationKeyframe&)>& applyFn) {
                        if (!animationApplyToSelection || editTargets.size() <= 1) return;
                        for (auto* obj : editTargets) {
                            if (!obj || obj->id == targetObj->id) continue;
                            int idx = findNearestKeyIndex(obj->animation, selectedKey->time, 0.02f);
                            if (idx < 0 || idx >= static_cast<int>(obj->animation.keyframes.size())) continue;
                            applyFn(obj->animation.keyframes[idx]);
                        }
                    };

                    if (ImGui::DragFloat3("Key Position", &selectedKey->position.x, 0.01f, -10000.0f, 10000.0f, "%.2f")) {
                        applyToMatchingKeys([&](AnimationKeyframe& k) { k.position = selectedKey->position; });
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    if (ImGui::DragFloat3("Key Rotation", &selectedKey->rotation.x, 0.05f, -3600.0f, 3600.0f, "%.2f")) {
                        applyToMatchingKeys([&](AnimationKeyframe& k) { k.rotation = selectedKey->rotation; });
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    if (ImGui::DragFloat3("Key Scale", &selectedKey->scale.x, 0.01f, 0.001f, 10000.0f, "%.3f")) {
                        selectedKey->scale.x = std::max(0.001f, selectedKey->scale.x);
                        selectedKey->scale.y = std::max(0.001f, selectedKey->scale.y);
                        selectedKey->scale.z = std::max(0.001f, selectedKey->scale.z);
                        applyToMatchingKeys([&](AnimationKeyframe& k) { k.scale = selectedKey->scale; });
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Blend");
                    int modeIndex = static_cast<int>(selectedKey->curveMode);
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::Combo("Mode", &modeIndex, curveModeLabels, IM_ARRAYSIZE(curveModeLabels))) {
                        selectedKey->curveMode = static_cast<AnimationCurveMode>(modeIndex);
                        applyToMatchingKeys([&](AnimationKeyframe& k) { k.curveMode = selectedKey->curveMode; });
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Apply Mode To All")) {
                        for (auto* obj : editTargets) {
                            if (!obj) continue;
                            for (auto& k : obj->animation.keyframes) {
                                k.curveMode = selectedKey->curveMode;
                            }
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }

                    if (selectedKey->curveMode == AnimationCurveMode::Preset) {
                        int interpIndex = static_cast<int>(selectedKey->interpolation);
                        ImGui::SetNextItemWidth(200.0f);
                        if (ImGui::Combo("Preset", &interpIndex, interpLabels, IM_ARRAYSIZE(interpLabels))) {
                            selectedKey->interpolation = static_cast<AnimationInterpolation>(interpIndex);
                            applyToMatchingKeys([&](AnimationKeyframe& k) { k.interpolation = selectedKey->interpolation; });
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Apply Preset To All")) {
                            for (auto* obj : editTargets) {
                                if (!obj) continue;
                                for (auto& k : obj->animation.keyframes) {
                                    k.interpolation = selectedKey->interpolation;
                                }
                            }
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    } else {
                        ImGui::TextDisabled("Out Handle (to next)");
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::SliderFloat2("Out", &selectedKey->bezierOut.x, 0.0f, 1.0f, "%.2f")) {
                            selectedKey->bezierOut.x = clampFloat(selectedKey->bezierOut.x, 0.0f, 1.0f);
                            selectedKey->bezierOut.y = clampFloat(selectedKey->bezierOut.y, 0.0f, 1.0f);
                            applyToMatchingKeys([&](AnimationKeyframe& k) { k.bezierOut = selectedKey->bezierOut; });
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                        ImGui::TextDisabled("In Handle (from prev)");
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::SliderFloat2("In", &selectedKey->bezierIn.x, 0.0f, 1.0f, "%.2f")) {
                            selectedKey->bezierIn.x = clampFloat(selectedKey->bezierIn.x, 0.0f, 1.0f);
                            selectedKey->bezierIn.y = clampFloat(selectedKey->bezierIn.y, 0.0f, 1.0f);
                            applyToMatchingKeys([&](AnimationKeyframe& k) { k.bezierIn = selectedKey->bezierIn; });
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

                            ImVec2 outHandle = toScreen(selectedKey->bezierOut);
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
                                float y = applyBezier(t, selectedKey->bezierOut, nextKey.bezierIn);
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
                                    selectedKey->bezierOut = clamped;
                                    applyToMatchingKeys([&](AnimationKeyframe& k) { k.bezierOut = selectedKey->bezierOut; });
                                } else {
                                    nextKey.bezierIn = clamped;
                                    if (animationApplyToSelection && editTargets.size() > 1) {
                                        for (auto* obj : editTargets) {
                                            if (!obj || obj->id == targetObj->id) continue;
                                            int idx = findNearestKeyIndex(obj->animation, nextKey.time, 0.02f);
                                            if (idx < 0 || idx >= static_cast<int>(obj->animation.keyframes.size())) continue;
                                            obj->animation.keyframes[idx].bezierIn = nextKey.bezierIn;
                                        }
                                    }
                                }
                                projectManager.currentProject.hasUnsavedChanges = true;
                            }

                            ImGui::EndChild();
                        }
                    }
                }
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Properties");
            static std::string selectedTrackPath;
            static int selectedTrackKeyIndex = -1;

            auto findTrackByPath = [&](AnimationComponent& component, const std::string& path) -> AnimationPropertyTrack* {
                for (auto& track : component.tracks) {
                    if (track.path == path) return &track;
                }
                return nullptr;
            };

            auto addTrackToTarget = [&](SceneObject& obj, const std::string& path, const std::string& label) {
                if (!obj.hasAnimation) return;
                if (findTrackByPath(obj.animation, path)) return;
                float currentValue = 0.0f;
                if (!getAnimFloatPropertyValue(obj, path, currentValue)) return;
                AnimationPropertyTrack track;
                track.path = path;
                track.label = label;
                track.defaultValue = currentValue;
                obj.animation.tracks.push_back(track);
            };

            if (ImGui::Button("Add Property")) {
                ImGui::OpenPopup("AddAnimPropertyPopup");
            }
            if (ImGui::BeginPopup("AddAnimPropertyPopup")) {
                auto candidates = buildAnimPropertyCandidates(*targetObj);
                if (candidates.empty()) {
                    ImGui::TextDisabled("No animatable properties for this target.");
                } else {
                    for (const auto& candidate : candidates) {
                        bool alreadyExists = false;
                        if (findTrackByPath(anim, candidate.path)) {
                            alreadyExists = true;
                        }
                        ImGui::BeginDisabled(alreadyExists);
                        if (ImGui::Selectable(candidate.label.c_str(), false)) {
                            for (auto* obj : editTargets) {
                                if (!obj) continue;
                                addTrackToTarget(*obj, candidate.path, candidate.label);
                            }
                            selectedTrackPath = candidate.path;
                            selectedTrackKeyIndex = -1;
                            projectManager.currentProject.hasUnsavedChanges = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndDisabled();
                    }
                }
                ImGui::EndPopup();
            }

            if (anim.tracks.empty()) {
                ImGui::TextDisabled("No property tracks yet. Add one and key it.");
            } else if (ImGui::BeginTable("AnimationPropertyTracks", 4,
                                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 118.0f);
                ImGui::TableHeadersRow();

                for (size_t ti = 0; ti < anim.tracks.size(); ++ti) {
                    auto& track = anim.tracks[ti];
                    ImGui::PushID(static_cast<int>(ti));
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    bool trackSelected = (selectedTrackPath == track.path);
                    if (ImGui::Selectable(track.label.empty() ? track.path.c_str() : track.label.c_str(),
                                          trackSelected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedTrackPath = track.path;
                        selectedTrackKeyIndex = -1;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", track.path.c_str());
                    }

                    ImGui::TableNextColumn();
                    float currentValue = 0.0f;
                    bool hasValue = getAnimFloatPropertyValue(*targetObj, track.path, currentValue);
                    ImGui::BeginDisabled(!hasValue || !track.enabled);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat("##TrackValue", &currentValue, 0.01f, -100000.0f, 100000.0f, "%.4g")) {
                        for (auto* obj : editTargets) {
                            if (!obj) continue;
                            bool transformDirty = false;
                            if (setAnimFloatPropertyValue(*obj, track.path, currentValue, &transformDirty) && transformDirty) {
                                syncLocalTransform(*obj);
                            }
                            if (AnimationPropertyTrack* otherTrack = findTrackByPath(obj->animation, track.path)) {
                                otherTrack->defaultValue = currentValue;
                                if (animationRecordMode) {
                                    upsertPropertyKey(*otherTrack, animationCurrentTime, currentValue);
                                }
                            }
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::EndDisabled();

                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", track.keyframes.size());

                    ImGui::TableNextColumn();
                    if (ImGui::Checkbox("##TrackEnabled", &track.enabled)) {
                        for (auto* obj : editTargets) {
                            if (!obj || obj->id == targetObj->id) continue;
                            if (AnimationPropertyTrack* otherTrack = findTrackByPath(obj->animation, track.path)) {
                                otherTrack->enabled = track.enabled;
                            }
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Key")) {
                        for (auto* obj : editTargets) {
                            if (!obj) continue;
                            AnimationPropertyTrack* targetTrack = findTrackByPath(obj->animation, track.path);
                            if (!targetTrack) continue;
                            float value = 0.0f;
                            if (!getAnimFloatPropertyValue(*obj, targetTrack->path, value)) continue;
                            upsertPropertyKey(*targetTrack, animationCurrentTime, value);
                            targetTrack->defaultValue = value;
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        std::string pathToRemove = track.path;
                        for (auto* obj : editTargets) {
                            if (!obj) continue;
                            auto& tracks = obj->animation.tracks;
                            tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [&](const AnimationPropertyTrack& t) { return t.path == pathToRemove; }), tracks.end());
                        }
                        if (selectedTrackPath == pathToRemove) {
                            selectedTrackPath.clear();
                            selectedTrackKeyIndex = -1;
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (!selectedTrackPath.empty()) {
                AnimationPropertyTrack* selectedTrack = findTrackByPath(anim, selectedTrackPath);
                if (selectedTrack) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Track Keys: %s",
                        selectedTrack->label.empty() ? selectedTrack->path.c_str() : selectedTrack->label.c_str());
                    ImGui::TextDisabled("Path: %s", selectedTrack->path.c_str());

                    if (selectedTrack->keyframes.empty()) {
                        ImGui::TextDisabled("No keys on selected track.");
                    } else if (ImGui::BeginTable("SelectedPropertyTrackKeys", 2,
                                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                        ImGui::TableSetupColumn("Time");
                        ImGui::TableSetupColumn("Value");
                        ImGui::TableHeadersRow();
                        for (size_t i = 0; i < selectedTrack->keyframes.size(); ++i) {
                            auto& key = selectedTrack->keyframes[i];
                            ImGui::PushID(static_cast<int>(i));
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            char timeLabel[32];
                            std::snprintf(timeLabel, sizeof(timeLabel), "%.2f", key.time);
                            if (ImGui::Selectable(timeLabel, selectedTrackKeyIndex == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                                selectedTrackKeyIndex = static_cast<int>(i);
                            }
                            ImGui::TableNextColumn();
                            ImGui::Text("%.4g", key.value);
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }

                    ImGui::BeginDisabled(selectedTrackKeyIndex < 0 ||
                                         selectedTrackKeyIndex >= static_cast<int>(selectedTrack->keyframes.size()));
                    if (ImGui::Button("Delete Track Key")) {
                        if (selectedTrackKeyIndex >= 0 && selectedTrackKeyIndex < static_cast<int>(selectedTrack->keyframes.size())) {
                            float keyTime = selectedTrack->keyframes[selectedTrackKeyIndex].time;
                            for (auto* obj : editTargets) {
                                if (!obj) continue;
                                AnimationPropertyTrack* otherTrack = findTrackByPath(obj->animation, selectedTrackPath);
                                if (!otherTrack) continue;
                                int idx = findNearestPropertyKeyIndex(*otherTrack, keyTime, 0.02f);
                                if (idx >= 0 && idx < static_cast<int>(otherTrack->keyframes.size())) {
                                    otherTrack->keyframes.erase(otherTrack->keyframes.begin() + idx);
                                }
                            }
                            selectedTrackKeyIndex = -1;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }
                    ImGui::EndDisabled();

                    if (selectedTrackKeyIndex >= 0 && selectedTrackKeyIndex < static_cast<int>(selectedTrack->keyframes.size())) {
                        auto& key = selectedTrack->keyframes[selectedTrackKeyIndex];
                        float oldTime = key.time;
                        if (ImGui::DragFloat("Track Key Time", &key.time, 0.01f, 0.0f, anim.clipLength, "%.2f")) {
                            key.time = clampFloat(key.time, 0.0f, anim.clipLength);
                            sortPropertyKeys(*selectedTrack);
                            selectedTrackKeyIndex = findNearestPropertyKeyIndex(*selectedTrack, key.time, 0.02f);
                            if (animationApplyToSelection && editTargets.size() > 1) {
                                for (auto* obj : editTargets) {
                                    if (!obj || obj->id == targetObj->id) continue;
                                    AnimationPropertyTrack* otherTrack = findTrackByPath(obj->animation, selectedTrackPath);
                                    if (!otherTrack) continue;
                                    int idx = findNearestPropertyKeyIndex(*otherTrack, oldTime, 0.02f);
                                    if (idx < 0 || idx >= static_cast<int>(otherTrack->keyframes.size())) continue;
                                    otherTrack->keyframes[idx].time = key.time;
                                    sortPropertyKeys(*otherTrack);
                                }
                            }
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                        if (ImGui::DragFloat("Track Key Value", &key.value, 0.01f, -100000.0f, 100000.0f, "%.4g")) {
                            if (animationApplyToSelection && editTargets.size() > 1) {
                                for (auto* obj : editTargets) {
                                    if (!obj || obj->id == targetObj->id) continue;
                                    AnimationPropertyTrack* otherTrack = findTrackByPath(obj->animation, selectedTrackPath);
                                    if (!otherTrack) continue;
                                    int idx = findNearestPropertyKeyIndex(*otherTrack, key.time, 0.02f);
                                    if (idx < 0 || idx >= static_cast<int>(otherTrack->keyframes.size())) continue;
                                    otherTrack->keyframes[idx].value = key.value;
                                }
                            }
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
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
                        animationSelectedEvent = -1;
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

            ImGui::Spacing();
            ImGui::SeparatorText("Events");
            if (ImGui::Button("Add Event")) {
                addEventAtCurrentTime(*targetObj);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(animationSelectedEvent < 0 || animationSelectedEvent >= static_cast<int>(anim.events.size()));
            if (ImGui::Button("Delete Event")) {
                if (animationSelectedEvent >= 0 && animationSelectedEvent < static_cast<int>(anim.events.size())) {
                    anim.events.erase(anim.events.begin() + animationSelectedEvent);
                    animationSelectedEvent = -1;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Sort Events")) {
                sortEvents(anim);
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            if (anim.events.empty()) {
                ImGui::TextDisabled("No events yet.");
            } else if (ImGui::BeginTable("AnimationEventTable", 3,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Event ID");
                ImGui::TableSetupColumn("Payload");
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < anim.events.size(); ++i) {
                    const auto& evt = anim.events[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    bool selected = (animationSelectedEvent == static_cast<int>(i));
                    char timeLabel[32];
                    std::snprintf(timeLabel, sizeof(timeLabel), "%.2f", evt.time);
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(timeLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        animationSelectedEvent = static_cast<int>(i);
                        animationSelectedKey = -1;
                        animationCurrentTime = evt.time;
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(evt.eventId.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(evt.payload.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            static int editEventTarget = -1;
            static int editEventIndex = -1;
            static char eventIdBuffer[128] = "";
            static char eventPayloadBuffer[256] = "";

            if (animationSelectedEvent >= 0 && animationSelectedEvent < static_cast<int>(anim.events.size())) {
                auto& evt = anim.events[animationSelectedEvent];
                if (editEventTarget != targetObj->id || editEventIndex != animationSelectedEvent) {
                    copyStringToBuffer(evt.eventId, eventIdBuffer, sizeof(eventIdBuffer));
                    copyStringToBuffer(evt.payload, eventPayloadBuffer, sizeof(eventPayloadBuffer));
                    editEventTarget = targetObj->id;
                    editEventIndex = animationSelectedEvent;
                }

                bool eventResorted = false;
                if (ImGui::DragFloat("Event Time", &evt.time, 0.01f, 0.0f, anim.clipLength, "%.2f")) {
                    evt.time = clampFloat(evt.time, 0.0f, anim.clipLength);
                    sortEvents(anim);
                    animationCurrentTime = evt.time;
                    animationSelectedEvent = findNearestEventIndex(anim, evt.time, 0.02f);
                    projectManager.currentProject.hasUnsavedChanges = true;
                    editEventIndex = -1;
                    eventResorted = true;
                }
                if (!eventResorted && ImGui::InputText("Event ID", eventIdBuffer, sizeof(eventIdBuffer))) {
                    evt.eventId = eventIdBuffer;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
                if (!eventResorted && ImGui::InputText("Payload", eventPayloadBuffer, sizeof(eventPayloadBuffer))) {
                    evt.payload = eventPayloadBuffer;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Config")) {
            if (ImGui::DragFloat("Clip Length", &anim.clipLength, 0.05f, 0.1f, 120.0f, "%.2f")) {
                anim.clipLength = std::max(0.1f, anim.clipLength);
                animationCurrentTime = clampFloat(animationCurrentTime, 0.0f, anim.clipLength);
                for (auto& key : anim.keyframes) {
                    key.time = clampFloat(key.time, 0.0f, anim.clipLength);
                }
                for (auto& evt : anim.events) {
                    evt.time = clampFloat(evt.time, 0.0f, anim.clipLength);
                }
                sortKeyframes(anim);
                sortEvents(anim);
                projectManager.currentProject.hasUnsavedChanges = true;
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
            ImGui::SameLine();
            if (ImGui::Button("Clear Events")) {
                anim.events.clear();
                animationSelectedEvent = -1;
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Tracks")) {
                anim.tracks.clear();
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    struct PropertyRecordState {
        float lastValue = 0.0f;
        bool initialized = false;
        bool pendingRelease = false;
    };
    static std::unordered_map<std::string, PropertyRecordState> propertyRecordState;
    if (animationRecordMode && !animationIsPlaying) {
        const bool mouseDragging =
            ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Middle);

        for (auto* obj : editTargets) {
            if (!obj || !obj->hasAnimation || !obj->animation.enabled) continue;
            for (auto& track : obj->animation.tracks) {
                if (!track.enabled || track.path.empty()) continue;
                float currentValue = 0.0f;
                if (!getAnimFloatPropertyValue(*obj, track.path, currentValue)) continue;

                std::string stateKey = std::to_string(obj->id) + "|" + track.path;
                auto& state = propertyRecordState[stateKey];
                if (!state.initialized) {
                    state.lastValue = currentValue;
                    state.initialized = true;
                }

                float delta = std::abs(currentValue - state.lastValue);
                bool changed = delta > 0.0005f;
                if (changed) {
                    if (mouseDragging) {
                        state.pendingRelease = true;
                    } else {
                        upsertPropertyKey(track, animationCurrentTime, currentValue);
                        state.pendingRelease = false;
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                } else if (!mouseDragging && state.pendingRelease) {
                    upsertPropertyKey(track, animationCurrentTime, currentValue);
                    state.pendingRelease = false;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
                state.lastValue = currentValue;
            }
        }
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

    float previousSampleTime = animationLastAppliedTime;
    bool wrappedThisFrame = false;
    if (animationIsPlaying && anim.clipLength > 0.0f) {
        animationCurrentTime += ImGui::GetIO().DeltaTime * anim.playSpeed;
        if (animationCurrentTime > anim.clipLength) {
            if (anim.loop) {
                animationCurrentTime = std::fmod(animationCurrentTime, anim.clipLength);
                wrappedThisFrame = true;
            } else {
                animationCurrentTime = anim.clipLength;
                animationIsPlaying = false;
            }
        }
    }

    bool timeChanged = std::abs(animationCurrentTime - animationLastAppliedTime) > 0.0001f;
    if (animationIsPlaying || timeChanged) {
        for (auto* obj : editTargets) {
            if (!obj || !obj->animation.enabled) continue;
            if (!animationIsPlaying && !obj->animation.applyOnScrub) continue;
            applyPoseAtTime(*obj, animationCurrentTime);
            applyPropertyTracksAtTime(*obj, animationCurrentTime);
        }
        if (anim.enabled && (animationIsPlaying || anim.applyOnScrub)) {
            dispatchAnimationEvents(*targetObj, previousSampleTime, animationCurrentTime, wrappedThisFrame);
        }
        animationLastAppliedTime = animationCurrentTime;
    }

    ImGui::EndChild();
    ImGui::EndTable();
    ImGui::End();
}
