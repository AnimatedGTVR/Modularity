#define MINIAUDIO_IMPLEMENTATION
#include "../include/ThirdParty/miniaudio.h"
#include "AudioSystem.h"
#include <cmath>

namespace {
constexpr size_t kPreviewBuckets = 800;
constexpr ma_uint32 kPreviewChunkFrames = 2048;
}

bool AudioSystem::init() {
    if (initialized) return true;
    ma_result res = ma_engine_init(nullptr, &engine);
    if (res != MA_SUCCESS) {
        std::cerr << "AudioSystem: failed to init miniaudio (" << res << ")\n";
        return false;
    }
    initialized = true;
    return true;
}

void AudioSystem::shutdown() {
    stopPreview();
    destroyActiveSounds();
    if (initialized) {
        ma_engine_uninit(&engine);
        initialized = false;
    }
}

void AudioSystem::destroyActiveSounds() {
    for (auto& kv : activeSounds) {
        ma_sound_uninit(&kv.second.sound);
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
            ma_sound_start(&activeSounds[obj.id].sound);
        }
    }
}

void AudioSystem::onPlayStop() {
    destroyActiveSounds();
}

bool AudioSystem::ensureSoundFor(const SceneObject& obj) {
    auto it = activeSounds.find(obj.id);
    if (it != activeSounds.end()) {
        if (it->second.clipPath == obj.audioSource.clipPath) {
            refreshSoundParams(obj, it->second);
            return true;
        }
        ma_sound_uninit(&it->second.sound);
        activeSounds.erase(it);
    }

    if (!initialized && !init()) return false;

    ActiveSound snd{};
    ma_result res = ma_sound_init_from_file(
        &engine,
        obj.audioSource.clipPath.c_str(),
        MA_SOUND_FLAG_STREAM,
        nullptr,
        nullptr,
        &snd.sound
    );
    if (res != MA_SUCCESS) {
        std::cerr << "AudioSystem: failed to load " << obj.audioSource.clipPath << " (" << res << ")\n";
        return false;
    }

    snd.clipPath = obj.audioSource.clipPath;
    snd.spatial = obj.audioSource.spatial;
    refreshSoundParams(obj, snd);
    activeSounds[obj.id] = std::move(snd);
    return true;
}

void AudioSystem::refreshSoundParams(const SceneObject& obj, ActiveSound& snd) {
    ma_sound_set_looping(&snd.sound, obj.audioSource.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&snd.sound, obj.audioSource.volume);
    ma_sound_set_spatialization_enabled(&snd.sound, obj.audioSource.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_min_distance(&snd.sound, obj.audioSource.minDistance);
    ma_sound_set_max_distance(&snd.sound, obj.audioSource.maxDistance);
    ma_sound_set_position(&snd.sound, obj.position.x, obj.position.y, obj.position.z);

    if (!ma_sound_is_playing(&snd.sound) && obj.audioSource.playOnStart && obj.audioSource.enabled) {
        ma_sound_start(&snd.sound);
    }
}

void AudioSystem::update(const std::vector<SceneObject>& objects, const Camera& listenerCamera, bool playing) {
    if (!initialized) return;

    ma_engine_listener_set_position(&engine, 0, listenerCamera.position.x, listenerCamera.position.y, listenerCamera.position.z);
    ma_engine_listener_set_direction(&engine, 0, listenerCamera.front.x, listenerCamera.front.y, listenerCamera.front.z);
    ma_engine_listener_set_world_up(&engine, 0, listenerCamera.up.x, listenerCamera.up.y, listenerCamera.up.z);

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
                ma_sound_uninit(&eraseIt->second.sound);
                activeSounds.erase(eraseIt);
            }
            continue;
        }

        if (ensureSoundFor(obj)) {
            refreshSoundParams(obj, activeSounds[obj.id]);
        }
    }

    for (auto it = activeSounds.begin(); it != activeSounds.end(); ) {
        if (stillPresent.find(it->first) == stillPresent.end()) {
            ma_sound_uninit(&it->second.sound);
            it = activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioSystem::playPreview(const std::string& path, float volume) {
    if (path.empty()) return false;
    if (!initialized && !init()) return false;

    stopPreview();
    ma_result res = ma_sound_init_from_file(&engine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &previewSound);
    if (res != MA_SUCCESS) {
        std::cerr << "AudioSystem: preview load failed for " << path << " (" << res << ")\n";
        return false;
    }
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

    std::vector<float> temp(kPreviewChunkFrames * preview.channels);
    ma_uint64 frameCursor = 0;
    size_t bucketIndex = 0;
    ma_uint64 bucketCursor = 0;
    float bucketMax = 0.0f;

    while (frameCursor < totalFrames && bucketIndex < preview.waveform.size()) {
        ma_uint64 framesToRead = std::min<ma_uint64>(kPreviewChunkFrames, totalFrames - frameCursor);
        ma_uint64 framesRead = 0;
        ma_result readResult = ma_decoder_read_pcm_frames(&decoder, temp.data(), framesToRead, &framesRead);
        if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
            break;
        }
        if (framesRead == 0) break;

        for (ma_uint64 f = 0; f < framesRead; ++f) {
            for (ma_uint32 c = 0; c < preview.channels; ++c) {
                float sample = temp[static_cast<size_t>(f * preview.channels + c)];
                bucketMax = std::max(bucketMax, std::fabs(sample));
            }
            bucketCursor++;
            frameCursor++;

            if (bucketCursor >= framesPerBucket) {
                if (bucketIndex < preview.waveform.size()) {
                    preview.waveform[bucketIndex] = std::clamp(bucketMax, 0.0f, 1.0f);
                    bucketIndex++;
                }
                bucketCursor = 0;
                bucketMax = 0.0f;
            }
        }
    }

    if (bucketIndex < preview.waveform.size() && bucketMax > 0.0f) {
        preview.waveform[bucketIndex] = std::clamp(bucketMax, 0.0f, 1.0f);
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
