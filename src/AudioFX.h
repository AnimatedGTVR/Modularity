#pragma once
// AudioFX - modular, chainable audio effects for the Modularity engine.
//
// This header defines the pure data model (chains + effects), the reusable DSP
// core (AudioFXProcessor) and the built-in preset library. It has no miniaudio
// dependency so the data model can live on SceneObject components and the DSP
// core can be reused for both the global master bus and per-source effect nodes.
//
// The AudioSystem wraps an AudioFXProcessor inside a miniaudio ma_node for both
// the global chain and each audio source that opts into local effects. Local
// effects run before the global chain, so the two combine predictably:
//     source -> local AudioFX -> (reverb zones) -> global AudioFX -> output
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

// Order matters for serialization / editor combo boxes; append new types, do not reorder.
enum class AudioFXType : int {
    LowPass = 0,   // freq = cutoff (Hz), q = resonance
    HighPass,      // freq = cutoff (Hz), q = resonance
    BassBoost,     // freq = shelf (Hz), gainDb = boost, param2 = looseness (0..1)
    Distortion,    // amount = drive, param1 = tone (0..1), mix = wet
    Reverb,        // param1 = roomSize (0..1), param2 = damping (0..1), mix = wet
    Echo,          // param1 = time (s), param2 = feedback (0..1), mix = wet
    Pitch,         // param3 = semitones (applied at the sound level where available)
    WowFlutter,    // param1 = rate (Hz), amount = depth (0..1) - tape wow/flutter
    NoiseHiss,     // amount = level (0..1) - cassette/vinyl style hiss
    Count
};

const char* AudioFXTypeName(AudioFXType type);

// A single effect stage. Kept trivially copyable (no heap) so it can be published
// to the audio thread as a fixed-size array without allocation.
struct AudioFXEffect {
    AudioFXType type = AudioFXType::LowPass;
    bool enabled = true;
    float freq = 1000.0f; // cutoff / shelf frequency (Hz)
    float q = 0.707f;     // filter resonance / Q
    float gainDb = 6.0f;  // shelf / boost gain (dB)
    float amount = 0.5f;  // drive / depth / level (0..1+ depending on type)
    float mix = 1.0f;     // wet/dry mix (0..1)
    float param1 = 0.3f;  // Echo time / Reverb room / WowFlutter rate / Distortion tone
    float param2 = 0.4f;  // Echo feedback / Reverb damping / BassBoost looseness
    float param3 = 0.0f;  // Pitch semitones / reserved
};

// An ordered chain of effect stages plus a couple of chain-wide controls.
struct AudioFXChain {
    std::string name;          // preset / display name (informational)
    bool enabled = true;       // master toggle for the whole chain
    float stereoWidth = 1.0f;  // 1 = full stereo, 0 = mono (stereo narrowing)
    std::vector<AudioFXEffect> effects;

    bool active() const { return enabled && !effects.empty(); }
    // Pitch is applied at the sound level (miniaudio native) rather than in the
    // DSP node; this returns the combined semitone offset of any Pitch stages.
    float pitchSemitones() const;
};

// ---------------------------------------------------------------------------
// Built-in presets. All presets are ordinary AudioFXChain values, so anything
// loaded from a preset stays fully editable at runtime and in the editor.
// ---------------------------------------------------------------------------
namespace AudioFXPresets {
    // Names in display order. "None" clears the chain.
    const std::vector<std::string>& names();
    // Returns the chain for a preset name (empty/None chain if unknown).
    AudioFXChain get(const std::string& name);
}

// ---------------------------------------------------------------------------
// Serialization - the chain is stored as a single compact string so it plugs
// into both the JSON-style scene serializer and the key=value project writer
// as one field, without touching their component-field tables per parameter.
// ---------------------------------------------------------------------------
std::string AudioFXSerialize(const AudioFXChain& chain);
AudioFXChain AudioFXParse(const std::string& text);

// ---------------------------------------------------------------------------
// DSP core. Reusable, self-contained (no miniaudio types). One instance drives
// one ma_node. Thread model: setChain() is called from the main thread and
// (re)allocates state under a lock; process() runs on the audio thread and only
// touches state while holding the same lock via try_lock (passthrough on
// contention, which is rare since chain edits are infrequent).
// ---------------------------------------------------------------------------
class AudioFXProcessor {
public:
    static constexpr int kMaxEffects = 12;

    // Called once the audio format is known (audio thread not yet running on it).
    void configure(int channels, int sampleRate);
    // Publish a new chain (main thread).
    void setChain(const AudioFXChain& chain);
    // Process an interleaved f32 buffer in place (audio thread).
    void process(float* io, int frameCount);
    // True if the currently published chain does any processing.
    bool active() const;

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct EffectState {
        // Biquad histories, one per channel (filters / bass boost).
        std::vector<float> x1, x2, y1, y2;
        Biquad coeff;
        // Delay line for Echo / WowFlutter, interleaved by channel.
        std::vector<float> delay;
        int delayFrames = 0;
        int delayPos = 0;
        // Compact reverb state (per channel): 2 combs + 1 allpass.
        std::vector<float> combA, combB, allpass;
        int combAFrames = 0, combBFrames = 0, allpassFrames = 0;
        int combAPos = 0, combBPos = 0, allpassPos = 0;
        std::vector<float> lp; // damping one-pole state per channel
        // Modulation / noise.
        float lfoPhase = 0.0f;
        float toneState = 0.0f; // distortion tone one-pole (shared mono is fine)
    };

    void rebuildLocked();
    void applyEffect(const AudioFXEffect& fx, EffectState& st, float* io, int frameCount);

    mutable std::mutex mutex_;
    int channels_ = 0;
    int sampleRate_ = 48000;

    // Published chain (guarded by mutex_).
    int count_ = 0;
    AudioFXEffect effects_[kMaxEffects];
    float stereoWidth_ = 1.0f;
    bool chainActive_ = false;
    std::vector<EffectState> state_;

    uint32_t rng_ = 0x1234567u; // white-noise LCG for hiss
    float nextNoise();
};
