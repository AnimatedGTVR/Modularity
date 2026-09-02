#include "HardwareProfile.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// glad wants to define this itself and windows.h got there first.
#ifdef APIENTRY
#undef APIENTRY
#endif
#elif defined(__linux__) || defined(__ANDROID__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "../include/Graphics/OpenGL.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace Modularity {
namespace HardwareProfile {
namespace {

// --- thresholds ------------------------------------------------------------
// These are the calibration points for the whole feature, so they live together
// where they can be re-tuned in one place. They are reasoned starting values, NOT
// measured constants: the intent is that a Bay Trail / Braswell class iGPU lands
// in Low and anything from the last several years lands in High. Re-check them
// against real hardware before treating any of them as settled.
constexpr float kLowFullscreenPassMs   = 3.0f;   // a single fullscreen pass this slow means post-FX is off the table
constexpr float kLowDrawCallBatchMs    = 12.0f;  // submission alone eating most of a 60fps frame
constexpr float kBalancedFullscreenMs  = 1.0f;
constexpr float kBalancedDrawCallMs    = 5.0f;

// A 60fps frame is 16.67ms. Assume the scene itself wants most of it and leave
// this much for fullscreen effect passes when reporting a budget to the user.
constexpr float kPassBudgetMsAt60      = 10.0f;

// Fixed internal probe size so results are comparable between machines and do
// not move when the user resizes the editor. Close enough to 1366x768 that the
// numbers mean something directly on the netbook class this targets.
constexpr int kProbeWidth  = 1280;
constexpr int kProbeHeight = 720;
constexpr int kBlurDownscale = 4;    // matches UiGlassBlur's quarter-res target

constexpr int kDrawCallProbeCount = 2000;
constexpr int kDrawCallViewport   = 16;   // tiny, so fragment cost ~= 0 and this measures submission

// Total wall time we aim to spend per timed probe. Iteration counts are derived
// from a calibration run so a slow GPU does not turn this into a 10 second hang.
constexpr float kTargetProbeMs = 150.0f;
constexpr int   kMinIterations = 4;
constexpr int   kMaxIterations = 64;
constexpr int   kWarmupIterations = 4;

StaticInfo   g_static;
bool         g_staticQueried = false;
// Computed once with the static probe. The popup and the Settings tab both poll
// this every frame while they are open, and rebuilding the string each time
// would be a per-frame allocation on exactly the machines that can least afford
// one.
std::string  g_lowEndReason;
BenchmarkResult g_lastBenchmark;

std::string ToLowerCopy(std::string v)
{
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}

bool Contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::string TrimCopy(const std::string& v)
{
    const size_t first = v.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = v.find_last_not_of(" \t\r\n");
    return v.substr(first, last - first + 1);
}

const char* GlStringOrEmpty(GLenum name)
{
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

unsigned long long QuerySystemMemoryMB()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<unsigned long long>(status.ullTotalPhys / (1024ull * 1024ull));
    }
    return 0;
#elif defined(__linux__) || defined(__ANDROID__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        return (static_cast<unsigned long long>(pages) *
                static_cast<unsigned long long>(pageSize)) / (1024ull * 1024ull);
    }
    return 0;
#elif defined(__APPLE__)
    int64_t bytes = 0;
    size_t len = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0 && bytes > 0) {
        return static_cast<unsigned long long>(bytes) / (1024ull * 1024ull);
    }
    return 0;
#else
    return 0;
#endif
}

// GPU families known to be weak. Matched against the real GL_RENDERER text, not
// against marketing codenames: Windows reports "Intel(R) HD Graphics 400", never
// "Braswell". Mesa is the one that appends a codename, in parens.
//
// Only a *suspicion* signal - the benchmark still gets the final word.
bool IsKnownWeakRenderer(const std::string& rendererLower)
{
    // Bay Trail reports a bare "Intel(R) HD Graphics" with no model number, so it
    // has to be an exact match. As a substring it would swallow every numbered
    // Intel part ever shipped, including the perfectly capable ones.
    const std::string trimmed = TrimCopy(rendererLower);
    if (trimmed == "intel(r) hd graphics" || trimmed == "intel hd graphics") {
        return true;
    }

    static const char* kWeak[] = {
        // Atom-class integrated parts
        "hd graphics 400", "hd graphics 405",   // Braswell (also catches HD 4000)
        "hd graphics 500", "hd graphics 505",   // Apollo Lake
        "uhd graphics 600", "uhd graphics 605", // Gemini Lake
        // Pre-Haswell desktop/mobile Intel
        "hd graphics 2000", "hd graphics 2500", "hd graphics 3000",
        // Mesa codename suffixes, e.g. "Mesa Intel(R) HD Graphics 400 (BSW)"
        "(byt)", "(bsw)", "(bxt)", "(glk)",
        "pineview", "gma",
        // Mobile
        "mali-4", "mali-t6", "mali-t7",
        "adreno (tm) 3", "adreno (tm) 4",
        "videocore",
    };
    for (const char* w : kWeak) {
        if (Contains(rendererLower, w)) return true;
    }
    return false;
}

bool IsSoftwareRenderer(const std::string& rendererLower)
{
    return Contains(rendererLower, "llvmpipe") ||
           Contains(rendererLower, "softpipe") ||
           Contains(rendererLower, "swrast") ||
           Contains(rendererLower, "software rasterizer") ||
           Contains(rendererLower, "gdi generic");
}

// --- GL scratch resources for the benchmark --------------------------------
const char* VersionDirective()
{
#if MODULARITY_OPENGL_ES
    return "#version 300 es\nprecision mediump float;\n";
#else
    return "#version 330\n";
#endif
}

GLuint CompileStage(GLenum type, const std::string& src)
{
    const char* c = src.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &c, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[WARN] HardwareProfile shader compile: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint LinkProgram(const std::string& vsSrc, const std::string& fsSrc)
{
    GLuint vs = CompileStage(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fsSrc);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Fullscreen triangle straight out of gl_VertexID - no vertex buffer needed,
// same trick UiGlassBlur uses.
std::string FullscreenVertexSrc()
{
    return std::string(VersionDirective()) +
        "out vec2 vUV;\n"
        "void main(){\n"
        "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    vUV = p;\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
}

// One texture fetch plus a little math: representative of a real post-FX pass
// rather than a degenerate one the driver could optimise away.
std::string FillFragmentSrc()
{
    return std::string(VersionDirective()) +
        "in vec2 vUV;\n"
        "out vec4 oColor;\n"
        "uniform sampler2D uTex;\n"
        "uniform float uSeed;\n"
        "void main(){\n"
        "    vec4 c = texture(uTex, vUV);\n"
        "    c.rgb = pow(c.rgb + uSeed * 0.0001, vec3(1.0 / 2.2));\n"
        "    oColor = vec4(c.rgb, 1.0);\n"
        "}\n";
}

// Deliberately identical in shape to UiGlassBlur's blur so that gating glass
// blur on this measurement is honest.
std::string BlurFragmentSrc()
{
    return std::string(VersionDirective()) +
        "in vec2 vUV;\n"
        "out vec4 oColor;\n"
        "uniform sampler2D uTex;\n"
        "uniform vec2 uDir;\n"
        "void main(){\n"
        "    vec3 c = texture(uTex, vUV).rgb * 0.227027;\n"
        "    c += (texture(uTex, vUV + uDir * 1.0).rgb + texture(uTex, vUV - uDir * 1.0).rgb) * 0.1945946;\n"
        "    c += (texture(uTex, vUV + uDir * 2.0).rgb + texture(uTex, vUV - uDir * 2.0).rgb) * 0.1216216;\n"
        "    c += (texture(uTex, vUV + uDir * 3.0).rgb + texture(uTex, vUV - uDir * 3.0).rgb) * 0.054054;\n"
        "    c += (texture(uTex, vUV + uDir * 4.0).rgb + texture(uTex, vUV - uDir * 4.0).rgb) * 0.016216;\n"
        "    oColor = vec4(c, 1.0);\n"
        "}\n";
}

std::string SolidFragmentSrc()
{
    return std::string(VersionDirective()) +
        "out vec4 oColor;\n"
        "uniform float uSeed;\n"
        "void main(){ oColor = vec4(uSeed, 0.25, 0.5, 1.0); }\n";
}

void AllocColorTexture(GLuint tex, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

using Clock = std::chrono::steady_clock;

float MillisSince(const Clock::time_point& start)
{
    const auto delta = Clock::now() - start;
    return std::chrono::duration<float, std::milli>(delta).count();
}

// Runs `body` once to see how slow it is, then picks an iteration count that
// keeps the timed run near kTargetProbeMs. Bounded both ways so a fast GPU still
// gets enough samples and a slow one never stalls the editor for seconds.
template <typename Fn>
int CalibrateIterations(Fn&& body)
{
    glFinish();
    const auto t0 = Clock::now();
    body(0);
    glFinish();
    const float oneMs = MillisSince(t0);
    if (oneMs <= 0.0001f) return kMaxIterations;
    const int n = static_cast<int>(kTargetProbeMs / oneMs);
    return std::max(kMinIterations, std::min(kMaxIterations, n));
}

// glFinish + wall clock rather than GL timer queries: ARB_timer_query is core in
// 3.3 but its driver support on exactly the old Intel parts this feature exists
// for is unreliable, and a crude number we trust beats a precise one we do not.
template <typename Fn>
float TimeAveraged(int iterations, Fn&& body, int warmup = kWarmupIterations)
{
    for (int i = 0; i < warmup; ++i) body(i);
    glFinish();

    const auto t0 = Clock::now();
    for (int i = 0; i < iterations; ++i) body(i);
    glFinish();
    const float total = MillisSince(t0);

    return iterations > 0 ? total / static_cast<float>(iterations) : 0.0f;
}

// Everything the probe clobbers, put back exactly as found. The benchmark can be
// triggered from a menu mid-session, so leaving state behind would corrupt the
// very next editor frame.
struct GLStateGuard {
    GLint drawFbo = 0, readFbo = 0, program = 0, vao = 0, arrayBuf = 0;
    GLint activeTex = 0, tex2d = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean blend = GL_FALSE, depth = GL_FALSE, cull = GL_FALSE, scissor = GL_FALSE;

    GLStateGuard()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
        glGetIntegerv(GL_VIEWPORT, viewport);
        blend   = glIsEnabled(GL_BLEND);
        depth   = glIsEnabled(GL_DEPTH_TEST);
        cull    = glIsEnabled(GL_CULL_FACE);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
    }

    ~GLStateGuard()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuf));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex2d));
        glActiveTexture(static_cast<GLenum>(activeTex));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (blend)   glEnable(GL_BLEND);   else glDisable(GL_BLEND);
        if (depth)   glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cull)    glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
        if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    }
};

// Defined further down, next to the wording it produces; declared here because
// Static() caches its result at probe time.
std::string ComputeLowEndReason(const StaticInfo& info);

Tier TierFromMeasurements(const BenchmarkResult& r)
{
    if (Static().softwareRasterizer) return Tier::Low;
    if (r.fullscreenPassMs > kLowFullscreenPassMs || r.drawCallBatchMs > kLowDrawCallBatchMs) {
        return Tier::Low;
    }
    if (r.fullscreenPassMs > kBalancedFullscreenMs || r.drawCallBatchMs > kBalancedDrawCallMs) {
        return Tier::Balanced;
    }
    return Tier::High;
}

} // namespace

// ---------------------------------------------------------------------------

const char* ToString(Tier tier)
{
    switch (tier) {
        case Tier::Low:      return "Low";
        case Tier::Balanced: return "Balanced";
        case Tier::High:
        default:             return "High";
    }
}

Tier FromString(const std::string& value)
{
    const std::string v = ToLowerCopy(value);
    if (v == "low" || v == "0")      return Tier::Low;
    if (v == "balanced" || v == "1") return Tier::Balanced;
    return Tier::High;
}

std::string StaticInfo::summary() const
{
    std::ostringstream out;
    out << (glRenderer.empty() ? "Unknown GPU" : glRenderer);
    if (glMajor > 0) {
        // Labelled "context" so this never reads as a hardware ceiling - it is the
        // version the engine requested, and every GPU reports the same number.
        out << " - " << OpenGLApiName() << " " << glMajor << "." << glMinor << " context";
    }
    if (cpuThreads > 0) {
        out << " - " << cpuThreads << (cpuThreads == 1 ? " thread" : " threads");
    }
    if (systemMemoryMB > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(systemMemoryMB) / 1024.0);
        out << " - " << buf;
    }
    return out.str();
}

const StaticInfo& Static()
{
    if (g_staticQueried) return g_static;

    // No context yet means glGetString returns null; stay zeroed and try again
    // on the next call rather than caching a useless answer.
    const char* renderer = GlStringOrEmpty(GL_RENDERER);
    if (renderer[0] == '\0') return g_static;

    g_static.glVendor   = GlStringOrEmpty(GL_VENDOR);
    g_static.glRenderer = renderer;
    g_static.glVersion  = GlStringOrEmpty(GL_VERSION);

    glGetIntegerv(GL_MAJOR_VERSION, &g_static.glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &g_static.glMinor);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_static.maxTextureSize);
    // Clear anything the queries above may have raised on a cranky old driver so
    // we do not hand a stale GL error to the next subsystem.
    while (glGetError() != GL_NO_ERROR) {}

    const std::string rendererLower = ToLowerCopy(g_static.glRenderer);
    g_static.softwareRasterizer = IsSoftwareRenderer(rendererLower);
    g_static.cpuThreads = std::thread::hardware_concurrency();
    g_static.systemMemoryMB = QuerySystemMemoryMB();

    g_lowEndReason = ComputeLowEndReason(g_static);
    g_staticQueried = true;
    return g_static;
}

void InvalidateStatic()
{
    g_staticQueried = false;
    g_static = StaticInfo();
    g_lowEndReason.clear();
}

namespace {

std::string ComputeLowEndReason(const StaticInfo& info)
{
    if (info.softwareRasterizer) {
        return "no GPU driver in use - rendering in software (" + info.glRenderer + ")";
    }
    const std::string rendererLower = ToLowerCopy(info.glRenderer);
    if (IsKnownWeakRenderer(rendererLower)) {
        return "known low-power GPU (" + info.glRenderer + ")";
    }
    // Nothing here may key off glMajor/glMinor. Window.cpp pins the context to
    // 3.3 core, so those report 3.3 on every GPU ever made and would flag the
    // entire world as low-end. Only limits and names survive that.
    if (info.maxTextureSize > 0 && info.maxTextureSize <= 8192) {
        return "GPU limits textures to " + std::to_string(info.maxTextureSize) +
               "px, typical of older integrated graphics";
    }
    if (info.cpuThreads > 0 && info.cpuThreads <= 2) {
        return std::to_string(info.cpuThreads) + "-thread CPU";
    }
    if (info.systemMemoryMB > 0 && info.systemMemoryMB <= 4096) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f GB of system memory",
                      static_cast<double>(info.systemMemoryMB) / 1024.0);
        return std::string(buf);
    }
    return std::string();
}

} // namespace

std::string LowEndReason()
{
    const StaticInfo& info = Static();
    // No context yet: stay silent rather than caching a verdict formed with no
    // information, so the next call can still answer properly.
    if (info.glRenderer.empty()) return std::string();
    if (!g_staticQueried) return std::string();
    return g_lowEndReason;
}

bool LooksLowEnd()
{
    const StaticInfo& info = Static();
    if (info.glRenderer.empty() || !g_staticQueried) return false;
    return !g_lowEndReason.empty();
}

std::string BenchmarkResult::summary() const
{
    if (!valid) return "not measured";
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%s - fullscreen pass %.2f ms, blur %.2f ms, %d draws %.2f ms "
                  "(~%.1f fullscreen passes per 60fps frame)",
                  ToString(tier), fullscreenPassMs, blurPassMs,
                  kDrawCallProbeCount, drawCallBatchMs, fullscreenPassBudget60);
    return std::string(buf);
}

const BenchmarkResult& LastBenchmark() { return g_lastBenchmark; }

void SetCachedBenchmark(const BenchmarkResult& result) { g_lastBenchmark = result; }

BenchmarkResult RunBenchmark()
{
    BenchmarkResult result;

    const StaticInfo& info = Static();
    if (info.glRenderer.empty()) {
        std::cerr << "[WARN] HardwareProfile: no GL context, skipping benchmark" << std::endl;
        return result;
    }

    GLStateGuard guard;

    int probeW = kProbeWidth;
    int probeH = kProbeHeight;
    if (info.maxTextureSize > 0) {
        probeW = std::min(probeW, info.maxTextureSize);
        probeH = std::min(probeH, info.maxTextureSize);
    }
    const int blurW = std::max(1, probeW / kBlurDownscale);
    const int blurH = std::max(1, probeH / kBlurDownscale);

    GLuint fillProg = LinkProgram(FullscreenVertexSrc(), FillFragmentSrc());
    GLuint blurProg = LinkProgram(FullscreenVertexSrc(), BlurFragmentSrc());
    GLuint solidProg = LinkProgram(FullscreenVertexSrc(), SolidFragmentSrc());
    if (fillProg == 0 || blurProg == 0 || solidProg == 0) {
        if (fillProg)  glDeleteProgram(fillProg);
        if (blurProg)  glDeleteProgram(blurProg);
        if (solidProg) glDeleteProgram(solidProg);
        std::cerr << "[WARN] HardwareProfile: probe shaders failed to build" << std::endl;
        return result;
    }

    GLuint vao = 0, srcTex = 0, dstTex = 0, blurTexA = 0, blurTexB = 0;
    GLuint dstFbo = 0, blurFboA = 0, blurFboB = 0;
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &srcTex);
    glGenTextures(1, &dstTex);
    glGenTextures(1, &blurTexA);
    glGenTextures(1, &blurTexB);
    glGenFramebuffers(1, &dstFbo);
    glGenFramebuffers(1, &blurFboA);
    glGenFramebuffers(1, &blurFboB);

    // Source content: a real gradient, so texture fetches cannot be folded away
    // and bilinear filtering has something to actually interpolate.
    {
        const int kSrc = 256;
        std::vector<unsigned char> pixels(static_cast<size_t>(kSrc) * kSrc * 4);
        for (int y = 0; y < kSrc; ++y) {
            for (int x = 0; x < kSrc; ++x) {
                const size_t i = (static_cast<size_t>(y) * kSrc + x) * 4;
                pixels[i + 0] = static_cast<unsigned char>(x);
                pixels[i + 1] = static_cast<unsigned char>(y);
                pixels[i + 2] = static_cast<unsigned char>((x ^ y) & 0xFF);
                pixels[i + 3] = 255;
            }
        }
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSrc, kSrc, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    AllocColorTexture(dstTex, probeW, probeH);
    AllocColorTexture(blurTexA, blurW, blurH);
    AllocColorTexture(blurTexB, blurW, blurH);

    auto attach = [](GLuint fbo, GLuint tex) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };

    const bool targetsOk = attach(dstFbo, dstTex) &&
                           attach(blurFboA, blurTexA) &&
                           attach(blurFboB, blurTexB);

    if (targetsOk) {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0);

        // -- fill rate ------------------------------------------------------
        glUseProgram(fillProg);
        glUniform1i(glGetUniformLocation(fillProg, "uTex"), 0);
        const GLint fillSeed = glGetUniformLocation(fillProg, "uSeed");
        auto fillPass = [&](int i) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
            glViewport(0, 0, probeW, probeH);
            glBindTexture(GL_TEXTURE_2D, srcTex);
            glUniform1f(fillSeed, static_cast<float>(i));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };
        result.fullscreenPassMs = TimeAveraged(CalibrateIterations(fillPass), fillPass);

        // -- blur (glass blur proxy) ----------------------------------------
        glUseProgram(blurProg);
        glUniform1i(glGetUniformLocation(blurProg, "uTex"), 0);
        const GLint blurDir = glGetUniformLocation(blurProg, "uDir");
        const float texelX = 1.0f / static_cast<float>(blurW);
        const float texelY = 1.0f / static_cast<float>(blurH);
        auto blurPass = [&](int i) {
            // horizontal then vertical, same ping-pong UiGlassBlur performs
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blurFboB);
            glViewport(0, 0, blurW, blurH);
            glBindTexture(GL_TEXTURE_2D, blurTexA);
            glUniform2f(blurDir, texelX, 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blurFboA);
            glBindTexture(GL_TEXTURE_2D, blurTexB);
            glUniform2f(blurDir, 0.0f, texelY);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            (void)i;
        };
        result.blurPassMs = TimeAveraged(CalibrateIterations(blurPass), blurPass);

        // -- draw call submission -------------------------------------------
        glUseProgram(solidProg);
        const GLint solidSeed = glGetUniformLocation(solidProg, "uSeed");
        auto drawBatch = [&](int i) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
            glViewport(0, 0, kDrawCallViewport, kDrawCallViewport);
            for (int d = 0; d < kDrawCallProbeCount; ++d) {
                // Uniform update per draw is deliberate: a real frame changes
                // state between draws, and that is most of the submission cost.
                glUniform1f(solidSeed, static_cast<float>(d & 0xFF) / 255.0f);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            (void)i;
        };
        // One batch is already ~2000 draws, so a handful of repeats is plenty and
        // a single warmup is enough - four would cost 8000 extra draws on exactly
        // the slow machines this probe exists to identify.
        const int batchIterations = std::max(2, std::min(8, CalibrateIterations(drawBatch)));
        result.drawCallBatchMs = TimeAveraged(batchIterations, drawBatch, 1);

        result.valid = true;
        result.fullscreenPassBudget60 = result.fullscreenPassMs > 0.0001f
            ? kPassBudgetMsAt60 / result.fullscreenPassMs
            : 0.0f;
        result.tier = TierFromMeasurements(result);
    } else {
        std::cerr << "[WARN] HardwareProfile: probe framebuffers incomplete" << std::endl;
    }

    glDeleteFramebuffers(1, &dstFbo);
    glDeleteFramebuffers(1, &blurFboA);
    glDeleteFramebuffers(1, &blurFboB);
    glDeleteTextures(1, &srcTex);
    glDeleteTextures(1, &dstTex);
    glDeleteTextures(1, &blurTexA);
    glDeleteTextures(1, &blurTexB);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(fillProg);
    glDeleteProgram(blurProg);
    glDeleteProgram(solidProg);
    while (glGetError() != GL_NO_ERROR) {}

    if (result.valid) {
        std::cout << "[Modularity] Hardware benchmark: " << result.summary() << std::endl;
        g_lastBenchmark = result;
    }
    return result;
}

} // namespace HardwareProfile
} // namespace Modularity
