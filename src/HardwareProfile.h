#pragma once

#include <string>

// "is this machine going to struggle?" - asked twice, two different ways.
//
//   Static()      cheap driver/OS questions, answered the moment a GL context exists.
//                 never enough to decide anything on its own: a renderer string tells
//                 you a name, not a frame time.
//   RunBenchmark() actually renders something and times it. this is the one that gets
//                 to set a Tier, because it measured the machine instead of guessing.
//
// LooksLowEnd() sits between them: it decides whether the machine is suspicious enough
// that we should bother *offering* the benchmark. a false positive there costs one
// dismissed popup, which is why it is allowed to be trigger-happy.
//
// runtime-safe on purpose - no ImGui, no editor headers. the player uses the same tier
// to pick its startup defaults, the editor just additionally has UI for it.
namespace Modularity {
namespace HardwareProfile {

enum class Tier {
    Low = 0,
    Balanced = 1,
    High = 2
};

const char* ToString(Tier tier);
Tier FromString(const std::string& value);

// ---------------------------------------------------------------------------
// Static probe
// ---------------------------------------------------------------------------
struct StaticInfo {
    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;
    // The version of the context Window.cpp asked for (3.3 core), NOT the driver's
    // maximum. Never use these to judge hardware: an RX 580 and a Bay Trail iGPU
    // both report 3.3 here, because that is what we requested.
    int   glMajor = 0;
    int   glMinor = 0;
    // Hardware/driver limit, NOT pinned by the context we asked for, so this is
    // one of the few capability signals here that means anything. Old integrated
    // parts cap at 8192; anything current reports 16384 or more.
    int   maxTextureSize = 0;
    // llvmpipe / softpipe / swrast / GDI Generic. the redist Mesa fallback lands here,
    // and it is the one case where no benchmark is needed to know the answer.
    bool  softwareRasterizer = false;
    unsigned cpuThreads = 0;
    unsigned long long systemMemoryMB = 0;

    // "Intel(R) HD Graphics - OpenGL 4.0 - 2 threads - 2.0 GB"
    std::string summary() const;
};

// Queried once against the current context and cached. Call with a GL context
// current; before that it returns a zeroed struct.
const StaticInfo& Static();

// Forces the next Static() call to re-query. Only needed if the context is
// recreated (backend switch), which is why nothing calls it in the normal path.
void InvalidateStatic();

// Worth offering the benchmark? Deliberately generous - see the note up top.
bool LooksLowEnd();

// Why LooksLowEnd() said yes, phrased for a human. Empty when it said no.
std::string LowEndReason();

// ---------------------------------------------------------------------------
// Microbenchmark
// ---------------------------------------------------------------------------
struct BenchmarkResult {
    bool  valid = false;

    // Milliseconds for ONE fullscreen textured pass at the fixed internal probe
    // resolution. This is the number that actually matters on an iGPU: post-FX,
    // bloom and glass blur are all priced in fullscreen passes.
    float fullscreenPassMs = 0.0f;
    // One separable 9-tap blur pass at quarter res - the exact shape UiGlassBlur
    // runs, so gating glass blur on this is an honest measurement rather than a
    // proxy.
    float blurPassMs = 0.0f;
    // Submission cost of kDrawCallProbeCount tiny draws into a 16x16 viewport.
    // Fragment work is negligible at that size, so this isolates CPU + driver
    // overhead, which is what actually caps draw calls on a 2-core machine.
    float drawCallBatchMs = 0.0f;

    // How many fullscreen passes fit in a 60fps frame, leaving room for the scene
    // itself. Presented to the user because it explains the verdict better than a
    // tier name does.
    float fullscreenPassBudget60 = 0.0f;

    Tier  tier = Tier::High;
    std::string summary() const;
};

// Renders entirely into its own offscreen FBO and never swaps, so the result is
// independent of whatever vsync/frame cap the caller has set. Saves and restores
// the GL state it touches; safe to call between frames with a context current.
//
// Takes roughly 200-400ms on a healthy GPU and up to a couple of seconds on a
// genuinely slow one. Do not call it from a frame you care about.
BenchmarkResult RunBenchmark();

// Last result from RunBenchmark() this process, or an invalid result if it has
// not run. Lets UI show the numbers without re-running the probe.
const BenchmarkResult& LastBenchmark();

// Restores a benchmark that was persisted from a previous session so the editor
// can show "measured earlier" numbers without re-probing on every launch.
void SetCachedBenchmark(const BenchmarkResult& result);

} // namespace HardwareProfile
} // namespace Modularity
