#define MINIAUDIO_IMPLEMENTATION
#include "../include/ThirdParty/miniaudio.h"
#include "AudioSystem.h"
#include <cmath>
#include <atomic>
#include <array>

namespace {
constexpr size_t kPreviewBuckets = 800;
constexpr ma_uint32 kPreviewChunkFrames = 2048;
constexpr float kReverbSmoothing = 0.12f;
constexpr size_t kReverbCombCount = 4;
constexpr size_t kReverbAllpassCount = 2;
constexpr float kReverbPreDelayMaxSeconds = 0.2f;
constexpr float kReverbReflectionsMaxSeconds = 0.1f;

float DbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

struct ReverbNodeVTable {
    ma_node_vtable vtable;
};

static void reverb_node_process(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn,
                                float** ppFramesOut, ma_uint32* pFrameCountOut) {
    if (!pNode || !ppFramesIn || !ppFramesOut) return;
    auto* node = reinterpret_cast<AudioSystem::SimpleReverbNode*>(pNode);
    (void)pFrameCountIn;
    const float* input = ppFramesIn[0];
    float* output = ppFramesOut[0];
    if (!input || !output) return;

    ma_uint32 frameCount = *pFrameCountOut;
    int channels = node->channels;
    float decayTime = std::max(0.1f, node->decayTime);
    float diffusion = std::clamp(node->diffusion, 0.0f, 100.0f);
    float density = std::clamp(node->density, 0.0f, 100.0f);
    float preDelaySeconds = std::clamp(node->preDelaySeconds, 0.0f, kReverbPreDelayMaxSeconds);
    float reflectionsDelaySeconds = std::clamp(node->reflectionsDelaySeconds, 0.0f, kReverbReflectionsMaxSeconds);
    size_t preDelayFrames = static_cast<size_t>(preDelaySeconds * static_cast<float>(node->sampleRate));
    size_t reflectionsDelayFrames = static_cast<size_t>(reflectionsDelaySeconds * static_cast<float>(node->sampleRate));
    float wetGain = std::clamp(node->wetGain, 0.0f, 2.0f);
    float reflectionsGain = std::clamp(node->reflectionsGain, 0.0f, 2.0f);

    float diffusionNorm = diffusion / 100.0f;
    float densityNorm = density / 100.0f;
    float allpassGain = 0.2f + 0.55f * diffusionNorm;
    float densityScale = 0.6f + 0.4f * densityNorm;
    float combGain = 1.0f / static_cast<float>(node->combBuffers.size());
    std::array<float, kReverbCombCount> combFeedback{};
    for (size_t i = 0; i < node->combBuffers.size(); ++i) {
        float delaySec = static_cast<float>(node->combBuffers[i].size() / channels) / static_cast<float>(node->sampleRate);
        combFeedback[i] = std::pow(10.0f, (-3.0f * delaySec) / decayTime) * densityScale;
    }

    float cutoffHz = std::clamp(node->hfReference * node->decayHFRatio, 500.0f, 20000.0f);
    float lpAlpha = std::exp(-2.0f * PI * cutoffHz / static_cast<float>(node->sampleRate));

    for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
        size_t preReadIndex = node->preDelayMaxFrames > 0
            ? (node->preDelayIndex + node->preDelayMaxFrames - preDelayFrames) % node->preDelayMaxFrames
            : 0;
        size_t reflectionsReadIndex = node->reflectionsMaxFrames > 0
            ? (node->reflectionsIndex + node->reflectionsMaxFrames - reflectionsDelayFrames) % node->reflectionsMaxFrames
            : 0;

        for (int ch = 0; ch < channels; ++ch) {
            float inSample = input[frame * channels + ch];
            float preSample = inSample;
            if (!node->preDelayBuffer.empty()) {
                size_t writeBase = node->preDelayIndex * channels;
                size_t readBase = preReadIndex * channels;
                preSample = node->preDelayBuffer[readBase + ch];
                node->preDelayBuffer[writeBase + ch] = inSample;
            }

            float reflectionsSample = 0.0f;
            if (!node->reflectionsBuffer.empty()) {
                size_t writeBase = node->reflectionsIndex * channels;
                size_t readBase = reflectionsReadIndex * channels;
                reflectionsSample = node->reflectionsBuffer[readBase + ch];
                node->reflectionsBuffer[writeBase + ch] = preSample;
            }

            float combSum = 0.0f;
            for (size_t i = 0; i < node->combBuffers.size(); ++i) {
                auto& buffer = node->combBuffers[i];
                size_t idx = node->combIndex[i];
                size_t base = idx * channels + ch;
                float y = buffer[base];
                buffer[base] = preSample + y * combFeedback[i];
                combSum += y;
            }

            combSum *= combGain;
            float apOut = combSum;
            for (size_t i = 0; i < node->allpassBuffers.size(); ++i) {
                auto& buffer = node->allpassBuffers[i];
                size_t idx = node->allpassIndex[i];
                size_t base = idx * channels + ch;
                float buf = buffer[base];
                float y = -allpassGain * apOut + buf;
                buffer[base] = apOut + buf * allpassGain;
                apOut = y;
            }

            float wetSample = apOut * wetGain + reflectionsSample * reflectionsGain;
            float lp = node->lpState.empty() ? wetSample : (lpAlpha * node->lpState[ch] + (1.0f - lpAlpha) * wetSample);
            if (!node->lpState.empty()) node->lpState[ch] = lp;
            output[frame * channels + ch] = lp;
        }

        if (!node->preDelayBuffer.empty()) {
            node->preDelayIndex = (node->preDelayIndex + 1) % node->preDelayMaxFrames;
        }
        if (!node->reflectionsBuffer.empty()) {
            node->reflectionsIndex = (node->reflectionsIndex + 1) % node->reflectionsMaxFrames;
        }
        for (size_t i = 0; i < node->combIndex.size(); ++i) {
            node->combIndex[i] = (node->combIndex[i] + 1) % (node->combBuffers[i].size() / channels);
        }
        for (size_t i = 0; i < node->allpassIndex.size(); ++i) {
            node->allpassIndex[i] = (node->allpassIndex[i] + 1) % (node->allpassBuffers[i].size() / channels);
        }
    }
}

static ma_result reverb_node_get_required_input_frames(ma_node* pNode, ma_uint32 outputFrameCount, ma_uint32* pInputFrameCount) {
    (void)pNode;
    if (pInputFrameCount) *pInputFrameCount = outputFrameCount;
    return MA_SUCCESS;
}

static ma_node_vtable g_reverb_node_vtable = {
    reverb_node_process,
    reverb_node_get_required_input_frames,
    1,
    1,
    0
};
}

bool AudioSystem::init() {
    if (initialized) return true;
    ma_result res = ma_engine_init(nullptr, &engine);
    if (res != MA_SUCCESS) {
        std::cerr << "AudioSystem: failed to init miniaudio (" << res << ")\n";
        return false;
    }
    ma_uint32 channels = ma_engine_get_channels(&engine);
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&engine);
    ma_splitter_node_config splitterConfig = ma_splitter_node_config_init(channels);
    res = ma_splitter_node_init(ma_engine_get_node_graph(&engine), &splitterConfig, nullptr, &reverbSplitter);
    if (res == MA_SUCCESS) {
        ma_node_config nodeConfig = ma_node_config_init();
        nodeConfig.vtable = &g_reverb_node_vtable;
        nodeConfig.pInputChannels = reinterpret_cast<const ma_uint32*>(&channels);
        nodeConfig.pOutputChannels = reinterpret_cast<const ma_uint32*>(&channels);
        res = ma_node_init(ma_engine_get_node_graph(&engine), &nodeConfig, nullptr, reinterpret_cast<ma_node*>(&reverbNode));
        if (res == MA_SUCCESS) {
            reverbNode.channels = static_cast<int>(channels);
            reverbNode.sampleRate = static_cast<int>(sampleRate);
            reverbNode.preDelayMaxFrames = static_cast<size_t>(kReverbPreDelayMaxSeconds * sampleRate);
            reverbNode.reflectionsMaxFrames = static_cast<size_t>(kReverbReflectionsMaxSeconds * sampleRate);
            reverbNode.preDelayBuffer.assign(reverbNode.preDelayMaxFrames * channels, 0.0f);
            reverbNode.reflectionsBuffer.assign(reverbNode.reflectionsMaxFrames * channels, 0.0f);
            reverbNode.lpState.assign(channels, 0.0f);

            const float combDelayMs[kReverbCombCount] = { 29.7f, 37.1f, 41.1f, 43.7f };
            reverbNode.combBuffers.resize(kReverbCombCount);
            reverbNode.combIndex.assign(kReverbCombCount, 0);
            for (size_t i = 0; i < kReverbCombCount; ++i) {
                size_t frames = static_cast<size_t>((combDelayMs[i] / 1000.0f) * sampleRate);
                frames = std::max<size_t>(1, frames);
                reverbNode.combBuffers[i].assign(frames * channels, 0.0f);
            }

            const float allpassDelayMs[kReverbAllpassCount] = { 5.0f, 1.7f };
            reverbNode.allpassBuffers.resize(kReverbAllpassCount);
            reverbNode.allpassIndex.assign(kReverbAllpassCount, 0);
            for (size_t i = 0; i < kReverbAllpassCount; ++i) {
                size_t frames = static_cast<size_t>((allpassDelayMs[i] / 1000.0f) * sampleRate);
                frames = std::max<size_t>(1, frames);
                reverbNode.allpassBuffers[i].assign(frames * channels, 0.0f);
            }

            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&reverbSplitter), 0, ma_engine_get_endpoint(&engine), 0);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&reverbSplitter), 1, reinterpret_cast<ma_node*>(&reverbNode), 0);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&reverbNode), 0, ma_engine_get_endpoint(&engine), 0);
            ma_sound_group_config groupConfig = ma_sound_group_config_init_2(&engine);
            groupConfig.pInitialAttachment = reinterpret_cast<ma_node*>(&reverbSplitter);
            groupConfig.initialAttachmentInputBusIndex = 0;
            res = ma_sound_group_init_ex(&engine, &groupConfig, &reverbGroup);
            if (res == MA_SUCCESS) {
                reverbReady = true;
                ma_sound_group_set_spatialization_enabled(&reverbGroup, MA_FALSE);
                ma_sound_group_set_attenuation_model(&reverbGroup, ma_attenuation_model_none);
                ma_sound_group_start(&reverbGroup);
                ma_node_set_output_bus_volume(reinterpret_cast<ma_node*>(&reverbSplitter), 0, 1.0f);
            } else {
                ma_node_uninit(reinterpret_cast<ma_node*>(&reverbNode), nullptr);
                ma_splitter_node_uninit(&reverbSplitter, nullptr);
            }
        } else {
            ma_splitter_node_uninit(&reverbSplitter, nullptr);
        }
    }

    initialized = true;
    return true;
}

void AudioSystem::shutdown() {
    stopPreview();
    destroyActiveSounds();
    shutdownReverbGraph();
    if (initialized) {
        ma_engine_uninit(&engine);
        initialized = false;
    }
}

void AudioSystem::destroyActiveSounds() {
    for (auto& kv : activeSounds) {
        if (kv.second) {
            ma_sound_uninit(&kv.second->sound);
        }
    }
    activeSounds.clear();
}

void AudioSystem::onPlayStart(const std::vector<SceneObject>& objects) {
    if (!initialized && !init()) return;
    destroyActiveSounds();
    for (const auto& obj : objects) {
        if (!obj.enabled || !obj.hasAudioSource || obj.audioSource.clipPath.empty()) continue;
        if (!obj.audioSource.enabled) continue;
        if (ensureSoundFor(obj) && obj.audioSource.playOnStart) {
            ma_sound_start(&activeSounds[obj.id]->sound);
        }
    }
}

void AudioSystem::onPlayStop() {
    destroyActiveSounds();
}

bool AudioSystem::ensureSoundFor(const SceneObject& obj) {
    auto it = activeSounds.find(obj.id);
    if (it != activeSounds.end()) {
        if (it->second && it->second->clipPath == obj.audioSource.clipPath) {
            refreshSoundParams(obj, *it->second);
            return true;
        }
        if (it->second) {
            ma_sound_uninit(&it->second->sound);
        }
        activeSounds.erase(it);
    }

    if (!fs::exists(obj.audioSource.clipPath)) {
        if (missingClips.insert(obj.audioSource.clipPath).second) {
            std::cerr << "AudioSystem: clip not found " << obj.audioSource.clipPath << "\n";
        }
        return false;
    }
    missingClips.erase(obj.audioSource.clipPath);

    if (!initialized && !init()) return false;

    auto snd = std::make_unique<ActiveSound>();
    ma_result res = ma_sound_init_from_file(
        &engine,
        obj.audioSource.clipPath.c_str(),
        MA_SOUND_FLAG_STREAM,
        reverbReady ? &reverbGroup : nullptr,
        nullptr,
        &snd->sound
    );
    if (res != MA_SUCCESS) {
        std::cerr << "AudioSystem: failed to load " << obj.audioSource.clipPath << " (" << res << ")\n";
        return false;
    }

    snd->clipPath = obj.audioSource.clipPath;
    snd->spatial = obj.audioSource.spatial;
    snd->started = false;
    refreshSoundParams(obj, *snd);
    activeSounds.emplace(obj.id, std::move(snd));
    return true;
}

void AudioSystem::refreshSoundParams(const SceneObject& obj, ActiveSound& snd) {
    float minDist = std::max(0.1f, obj.audioSource.minDistance);
    float maxDist = std::max(obj.audioSource.maxDistance, minDist + 0.5f);
    ma_sound_set_looping(&snd.sound, obj.audioSource.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&snd.sound, obj.audioSource.volume);
    ma_sound_set_spatialization_enabled(&snd.sound, obj.audioSource.spatial ? MA_TRUE : MA_FALSE);
    if (obj.audioSource.spatial) {
        switch (obj.audioSource.rolloffMode) {
            case AudioRolloffMode::Linear:
                ma_sound_set_attenuation_model(&snd.sound, ma_attenuation_model_linear);
                break;
            case AudioRolloffMode::Exponential:
                ma_sound_set_attenuation_model(&snd.sound, ma_attenuation_model_exponential);
                break;
            case AudioRolloffMode::Custom:
                ma_sound_set_attenuation_model(&snd.sound, ma_attenuation_model_none);
                break;
            case AudioRolloffMode::Logarithmic:
            default:
                ma_sound_set_attenuation_model(&snd.sound, ma_attenuation_model_inverse);
                break;
        }
        ma_sound_set_rolloff(&snd.sound, std::max(0.01f, obj.audioSource.rolloff));
    } else {
        ma_sound_set_attenuation_model(&snd.sound, ma_attenuation_model_none);
    }
    ma_sound_set_min_distance(&snd.sound, minDist);
    ma_sound_set_max_distance(&snd.sound, maxDist);
    ma_sound_set_position(&snd.sound, obj.position.x, obj.position.y, obj.position.z);

    if (!ma_sound_is_playing(&snd.sound) && !snd.started && obj.audioSource.playOnStart && obj.audioSource.enabled) {
        ma_sound_start(&snd.sound);
        snd.started = true;
    }
}

void AudioSystem::update(const std::vector<SceneObject>& objects, const Camera& listenerCamera, bool playing) {
    if (!initialized) return;

    ma_engine_listener_set_position(&engine, 0, listenerCamera.position.x, listenerCamera.position.y, listenerCamera.position.z);
    ma_engine_listener_set_direction(&engine, 0, listenerCamera.front.x, listenerCamera.front.y, listenerCamera.front.z);
    ma_engine_listener_set_world_up(&engine, 0, listenerCamera.up.x, listenerCamera.up.y, listenerCamera.up.z);
    updateReverb(objects, listenerCamera.position);

    if (!playing) {
        destroyActiveSounds();
        return;
    }

    std::unordered_set<int> stillPresent;
    for (const auto& obj : objects) {
        if (!obj.hasAudioSource) continue;
        stillPresent.insert(obj.id);

        auto eraseIt = activeSounds.find(obj.id);
        if (!obj.enabled || !obj.audioSource.enabled || obj.audioSource.clipPath.empty()) {
            if (eraseIt != activeSounds.end()) {
                if (eraseIt->second) {
                    ma_sound_uninit(&eraseIt->second->sound);
                }
                activeSounds.erase(eraseIt);
            }
            continue;
        }

        if (ensureSoundFor(obj)) {
            refreshSoundParams(obj, *activeSounds[obj.id]);
            if (obj.audioSource.spatial && obj.audioSource.rolloffMode == AudioRolloffMode::Custom) {
                float attenuation = computeCustomAttenuation(obj, listenerCamera.position);
                ma_sound_set_volume(&activeSounds[obj.id]->sound, obj.audioSource.volume * attenuation);
            }
        }
    }

    for (auto it = activeSounds.begin(); it != activeSounds.end(); ) {
        if (stillPresent.find(it->first) == stillPresent.end()) {
            if (it->second) {
                ma_sound_uninit(&it->second->sound);
            }
            it = activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioSystem::playPreview(const std::string& path, float volume, bool loop) {
    if (path.empty()) return false;
    if (!initialized && !init()) return false;

    stopPreview();
    ma_result res = ma_sound_init_from_file(&engine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &previewSound);
    if (res != MA_SUCCESS) {
        std::cerr << "AudioSystem: preview load failed for " << path << " (" << res << ")\n";
        return false;
    }
    ma_sound_set_looping(&previewSound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&previewSound, volume);
    ma_sound_set_spatialization_enabled(&previewSound, MA_FALSE);
    previewPath = path;
    previewActive = ma_sound_start(&previewSound) == MA_SUCCESS;
    if (!previewActive) {
        ma_sound_uninit(&previewSound);
    }
    return previewActive;
}

void AudioSystem::stopPreview() {
    if (previewActive) {
        ma_sound_stop(&previewSound);
        ma_sound_uninit(&previewSound);
    }
    previewActive = false;
    previewPath.clear();
}

bool AudioSystem::isPreviewing(const std::string& path) const {
    return previewActive && previewPath == path;
}

bool AudioSystem::getPreviewTime(const std::string& path, double& cursorSeconds, double& durationSeconds) const {
    if (!previewActive || previewPath != path) return false;
    float cur = 0.0f;
    float len = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&previewSound, &cur) != MA_SUCCESS) return false;
    if (ma_sound_get_length_in_seconds(&previewSound, &len) != MA_SUCCESS) return false;
    cursorSeconds = static_cast<double>(cur);
    durationSeconds = static_cast<double>(len);
    return true;
}

bool AudioSystem::seekPreview(const std::string& path, double seconds) {
    if (!previewActive || previewPath != path) return false;
    ma_uint32 sampleRate = 0;
    if (ma_sound_get_data_format(&previewSound, nullptr, nullptr, &sampleRate, nullptr, 0) != MA_SUCCESS) {
        return false;
    }
    float lenSec = 0.0f;
    ma_sound_get_length_in_seconds(&previewSound, &lenSec);
    seconds = std::clamp(seconds, 0.0, static_cast<double>(lenSec));
    ma_uint64 targetFrame = static_cast<ma_uint64>(seconds * static_cast<double>(sampleRate));
    ma_result res = ma_sound_seek_to_pcm_frame(&previewSound, targetFrame);
    return res == MA_SUCCESS;
}

bool AudioSystem::setPreviewLoop(bool loop) {
    if (!previewActive) return false;
    ma_sound_set_looping(&previewSound, loop ? MA_TRUE : MA_FALSE);
    return true;
}

bool AudioSystem::playObjectSound(const SceneObject& obj) {
    if (!obj.hasAudioSource || obj.audioSource.clipPath.empty() || !obj.audioSource.enabled) return false;
    if (!ensureSoundFor(obj)) return false;
    ActiveSound& snd = *activeSounds[obj.id];
    snd.started = true;
    return ma_sound_start(&snd.sound) == MA_SUCCESS;
}

bool AudioSystem::stopObjectSound(int objectId) {
    auto it = activeSounds.find(objectId);
    if (it == activeSounds.end()) return false;
    if (!it->second) return false;
    return ma_sound_stop(&it->second->sound) == MA_SUCCESS;
}

bool AudioSystem::setObjectLoop(const SceneObject& obj, bool loop) {
    if (!ensureSoundFor(obj)) return false;
    ActiveSound& snd = *activeSounds[obj.id];
    ma_sound_set_looping(&snd.sound, loop ? MA_TRUE : MA_FALSE);
    return true;
}

bool AudioSystem::setObjectVolume(const SceneObject& obj, float volume) {
    if (!ensureSoundFor(obj)) return false;
    ActiveSound& snd = *activeSounds[obj.id];
    ma_sound_set_volume(&snd.sound, volume);
    return true;
}

float AudioSystem::computeCustomAttenuation(const SceneObject& obj, const glm::vec3& listenerPos) const {
    float minDist = std::max(0.1f, obj.audioSource.minDistance);
    float maxDist = std::max(obj.audioSource.maxDistance, minDist + 0.5f);
    float dist = glm::length(listenerPos - obj.position);
    if (dist <= minDist) return 1.0f;
    if (dist >= maxDist) return std::clamp(obj.audioSource.customEndGain, 0.0f, 1.0f);

    float range = maxDist - minDist;
    float midRatio = std::clamp(obj.audioSource.customMidDistance, 0.0f, 1.0f);
    float midDist = minDist + range * midRatio;
    float midGain = std::clamp(obj.audioSource.customMidGain, 0.0f, 1.0f);
    float endGain = std::clamp(obj.audioSource.customEndGain, 0.0f, 1.0f);

    if (dist <= midDist) {
        float t = (dist - minDist) / std::max(0.001f, midDist - minDist);
        return std::clamp(1.0f + (midGain - 1.0f) * t, 0.0f, 1.0f);
    }

    float t = (dist - midDist) / std::max(0.001f, maxDist - midDist);
    return std::clamp(midGain + (endGain - midGain) * t, 0.0f, 1.0f);
}

void AudioSystem::updateReverb(const std::vector<SceneObject>& objects, const glm::vec3& listenerPos) {
    if (!reverbReady) return;
    float blend = 0.0f;
    ReverbSettings target = getReverbTarget(objects, listenerPos, blend);
    applyReverbSettings(target, blend);
}

AudioSystem::ReverbSettings AudioSystem::getReverbTarget(const std::vector<SceneObject>& objects, const glm::vec3& listenerPos, float& outBlend) const {
    ReverbSettings target{};
    float bestBlend = 0.0f;

    for (const auto& obj : objects) {
        if (!obj.enabled || !obj.hasReverbZone || !obj.reverbZone.enabled) continue;
        const auto& zone = obj.reverbZone;
        float blend = 0.0f;

        if (zone.shape == ReverbZoneShape::Sphere) {
            float minDist = std::max(0.0f, zone.minDistance);
            float maxDist = std::max(zone.maxDistance, minDist + 0.01f);
            float radius = std::max(0.01f, zone.radius);
            float dist = glm::length(listenerPos - obj.position);
            if (dist > radius) continue;
            maxDist = std::min(maxDist, radius);
            if (dist >= maxDist) continue;
            if (dist <= minDist) {
                blend = 1.0f;
            } else {
                blend = std::clamp((maxDist - dist) / (maxDist - minDist), 0.0f, 1.0f);
            }
        } else {
            glm::vec3 halfSize = glm::max(zone.boxSize * 0.5f, glm::vec3(0.01f));
            glm::vec3 delta = glm::abs(listenerPos - obj.position);
            if (delta.x > halfSize.x || delta.y > halfSize.y || delta.z > halfSize.z) continue;
            float edgeDistance = std::min({halfSize.x - delta.x, halfSize.y - delta.y, halfSize.z - delta.z});
            if (zone.blendDistance <= 0.001f) {
                blend = 1.0f;
            } else {
                blend = std::clamp(edgeDistance / zone.blendDistance, 0.0f, 1.0f);
            }
        }

        if (blend > bestBlend) {
            bestBlend = blend;
            target.room = zone.room;
            target.roomHF = zone.roomHF;
            target.roomLF = zone.roomLF;
            target.decayTime = zone.decayTime;
            target.decayHFRatio = zone.decayHFRatio;
            target.reflections = zone.reflections;
            target.reflectionsDelay = zone.reflectionsDelay;
            target.reverb = zone.reverb;
            target.reverbDelay = zone.reverbDelay;
            target.hfReference = zone.hfReference;
            target.lfReference = zone.lfReference;
            target.roomRolloffFactor = zone.roomRolloffFactor;
            target.diffusion = zone.diffusion;
            target.density = zone.density;
        }
    }

    outBlend = bestBlend;
    return target;
}

void AudioSystem::applyReverbSettings(const ReverbSettings& target, float blend) {
    ReverbSettings mixed{};
    mixed.room = target.room;
    mixed.roomHF = target.roomHF;
    mixed.roomLF = target.roomLF;
    mixed.decayTime = std::max(0.1f, target.decayTime);
    mixed.decayHFRatio = std::clamp(target.decayHFRatio, 0.1f, 2.0f);
    mixed.reflections = target.reflections;
    mixed.reflectionsDelay = std::clamp(target.reflectionsDelay, 0.0f, kReverbReflectionsMaxSeconds);
    mixed.reverb = target.reverb;
    mixed.reverbDelay = std::clamp(target.reverbDelay, 0.0f, kReverbPreDelayMaxSeconds);
    mixed.hfReference = std::clamp(target.hfReference, 1000.0f, 20000.0f);
    mixed.lfReference = std::clamp(target.lfReference, 20.0f, 1000.0f);
    mixed.roomRolloffFactor = std::max(0.0f, target.roomRolloffFactor);
    mixed.diffusion = std::clamp(target.diffusion, 0.0f, 100.0f);
    mixed.density = std::clamp(target.density, 0.0f, 100.0f);

    currentReverb.room = currentReverb.room + (mixed.room - currentReverb.room) * kReverbSmoothing;
    currentReverb.roomHF = currentReverb.roomHF + (mixed.roomHF - currentReverb.roomHF) * kReverbSmoothing;
    currentReverb.roomLF = currentReverb.roomLF + (mixed.roomLF - currentReverb.roomLF) * kReverbSmoothing;
    currentReverb.decayTime = currentReverb.decayTime + (mixed.decayTime - currentReverb.decayTime) * kReverbSmoothing;
    currentReverb.decayHFRatio = currentReverb.decayHFRatio + (mixed.decayHFRatio - currentReverb.decayHFRatio) * kReverbSmoothing;
    currentReverb.reflections = currentReverb.reflections + (mixed.reflections - currentReverb.reflections) * kReverbSmoothing;
    currentReverb.reflectionsDelay = currentReverb.reflectionsDelay + (mixed.reflectionsDelay - currentReverb.reflectionsDelay) * kReverbSmoothing;
    currentReverb.reverb = currentReverb.reverb + (mixed.reverb - currentReverb.reverb) * kReverbSmoothing;
    currentReverb.reverbDelay = currentReverb.reverbDelay + (mixed.reverbDelay - currentReverb.reverbDelay) * kReverbSmoothing;
    currentReverb.hfReference = currentReverb.hfReference + (mixed.hfReference - currentReverb.hfReference) * kReverbSmoothing;
    currentReverb.lfReference = currentReverb.lfReference + (mixed.lfReference - currentReverb.lfReference) * kReverbSmoothing;
    currentReverb.roomRolloffFactor = currentReverb.roomRolloffFactor + (mixed.roomRolloffFactor - currentReverb.roomRolloffFactor) * kReverbSmoothing;
    currentReverb.diffusion = currentReverb.diffusion + (mixed.diffusion - currentReverb.diffusion) * kReverbSmoothing;
    currentReverb.density = currentReverb.density + (mixed.density - currentReverb.density) * kReverbSmoothing;

    constexpr float kDbSoftening = 0.5f;
    constexpr float kWetScale = 0.25f;
    float reflectionsGain = DbToLinear((currentReverb.reflections + currentReverb.room) * kDbSoftening) * (blend * kWetScale);
    float reverbGain = DbToLinear((currentReverb.reverb + currentReverb.room) * kDbSoftening) * (blend * kWetScale);
    float dry = std::clamp(1.0f - blend * (currentReverb.roomRolloffFactor * 0.05f), 0.2f, 1.0f);

    ma_node_set_output_bus_volume(reinterpret_cast<ma_node*>(&reverbSplitter), 0, dry);
    reverbNode.wetGain = std::clamp(reverbGain, 0.0f, 1.0f);
    reverbNode.reflectionsGain = std::clamp(reflectionsGain, 0.0f, 1.0f);
    reverbNode.decayTime = currentReverb.decayTime;
    reverbNode.decayHFRatio = currentReverb.decayHFRatio;
    reverbNode.diffusion = currentReverb.diffusion;
    reverbNode.density = currentReverb.density;
    reverbNode.hfReference = currentReverb.hfReference;
    reverbNode.preDelaySeconds = currentReverb.reverbDelay;
    reverbNode.reflectionsDelaySeconds = currentReverb.reflectionsDelay;
}

void AudioSystem::shutdownReverbGraph() {
    if (reverbReady) {
        ma_sound_group_uninit(&reverbGroup);
        ma_node_uninit(reinterpret_cast<ma_node*>(&reverbNode), nullptr);
        ma_splitter_node_uninit(&reverbSplitter, nullptr);
        reverbReady = false;
    }
    currentReverb = ReverbSettings{};
}

AudioClipPreview AudioSystem::loadPreview(const std::string& path) {
    AudioClipPreview preview;
    preview.path = path;

    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) != MA_SUCCESS) {
        return preview;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    preview.channels = decoder.outputChannels;
    preview.sampleRate = decoder.outputSampleRate;
    preview.durationSeconds = (preview.sampleRate > 0) ? static_cast<double>(totalFrames) / static_cast<double>(preview.sampleRate) : 0.0;

    if (totalFrames == 0 || preview.channels == 0) {
        ma_decoder_uninit(&decoder);
        return preview;
    }

    const ma_uint64 framesPerBucket = std::max<ma_uint64>(1, totalFrames / kPreviewBuckets);
    preview.waveform.assign(static_cast<size_t>(kPreviewBuckets), 0.0f);
    if (preview.channels >= 2) {
        preview.waveformLeft.assign(static_cast<size_t>(kPreviewBuckets), 0.0f);
        preview.waveformRight.assign(static_cast<size_t>(kPreviewBuckets), 0.0f);
    }

    std::vector<float> temp(kPreviewChunkFrames * preview.channels);
    ma_uint64 frameCursor = 0;
    size_t bucketIndex = 0;
    ma_uint64 bucketCursor = 0;
    float bucketMax = 0.0f;
    float bucketMaxLeft = 0.0f;
    float bucketMaxRight = 0.0f;

    while (frameCursor < totalFrames && bucketIndex < preview.waveform.size()) {
        ma_uint64 framesToRead = std::min<ma_uint64>(kPreviewChunkFrames, totalFrames - frameCursor);
        ma_uint64 framesRead = 0;
        ma_result readResult = ma_decoder_read_pcm_frames(&decoder, temp.data(), framesToRead, &framesRead);
        if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
            break;
        }
        if (framesRead == 0) break;

        for (ma_uint64 f = 0; f < framesRead; ++f) {
            size_t frameOffset = static_cast<size_t>(f * preview.channels);
            for (ma_uint32 c = 0; c < preview.channels; ++c) {
                float sample = temp[frameOffset + c];
                bucketMax = std::max(bucketMax, std::fabs(sample));
            }
            if (preview.channels >= 2) {
                float leftSample = temp[frameOffset];
                float rightSample = temp[frameOffset + 1];
                bucketMaxLeft = std::max(bucketMaxLeft, std::fabs(leftSample));
                bucketMaxRight = std::max(bucketMaxRight, std::fabs(rightSample));
            }
            bucketCursor++;
            frameCursor++;

            if (bucketCursor >= framesPerBucket) {
                if (bucketIndex < preview.waveform.size()) {
                    preview.waveform[bucketIndex] = std::clamp(bucketMax, 0.0f, 1.0f);
                    if (preview.channels >= 2) {
                        preview.waveformLeft[bucketIndex] = std::clamp(bucketMaxLeft, 0.0f, 1.0f);
                        preview.waveformRight[bucketIndex] = std::clamp(bucketMaxRight, 0.0f, 1.0f);
                    }
                    bucketIndex++;
                }
                bucketCursor = 0;
                bucketMax = 0.0f;
                bucketMaxLeft = 0.0f;
                bucketMaxRight = 0.0f;
            }
        }
    }

    if (bucketIndex < preview.waveform.size() && bucketMax > 0.0f) {
        preview.waveform[bucketIndex] = std::clamp(bucketMax, 0.0f, 1.0f);
        if (preview.channels >= 2) {
            preview.waveformLeft[bucketIndex] = std::clamp(bucketMaxLeft, 0.0f, 1.0f);
            preview.waveformRight[bucketIndex] = std::clamp(bucketMaxRight, 0.0f, 1.0f);
        }
    }

    ma_decoder_uninit(&decoder);
    preview.loaded = true;
    return preview;
}

const AudioClipPreview* AudioSystem::getPreview(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = previewCache.find(path);
    if (it == previewCache.end()) {
        previewCache[path] = loadPreview(path);
        it = previewCache.find(path);
    }
    if (it != previewCache.end() && it->second.loaded) {
        return &it->second;
    }
    return nullptr;
}
