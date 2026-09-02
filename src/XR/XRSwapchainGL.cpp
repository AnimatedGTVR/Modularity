#include "XRSwapchainGL.h"

#include "XRDiagnostics.h"
#include "XRLoader.h"

#include "../../include/Graphics/OpenGL.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Modularity::XR {

namespace {

// GL internal formats we know how to name and rank. Values are spelled out rather
// than taken from the GL headers so this table reads the same in a desktop GL
// build and a GLES build, where the enum spellings differ in availability.
constexpr int64_t kGlRgba8 = 0x8058;         // GL_RGBA8
constexpr int64_t kGlSrgb8Alpha8 = 0x8C43;   // GL_SRGB8_ALPHA8
constexpr int64_t kGlRgb10A2 = 0x8059;       // GL_RGB10_A2
constexpr int64_t kGlRgba16f = 0x881A;       // GL_RGBA16F

} // namespace

std::string XRSwapchainFormatName(int64_t format) {
    switch (format) {
        case kGlRgba8:       return "GL_RGBA8";
        case kGlSrgb8Alpha8: return "GL_SRGB8_ALPHA8";
        case kGlRgb10A2:     return "GL_RGB10_A2";
        case kGlRgba16f:     return "GL_RGBA16F";
        default: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%llX",
                          static_cast<unsigned long long>(format));
            return buffer;
        }
    }
}

bool XRMultiviewSupported() {
    // Queried once. glGetStringi over the extension list is not free, and this is
    // consulted on every swapchain rebuild.
    static int cached = -1;
    if (cached >= 0) return cached != 0;

    cached = 0;
    GLint extensionCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    // A GL error here means no context is current, in which case the honest answer
    // is "unknown", and reporting false keeps us on the always-valid Multi Pass path.
    if (glGetError() != GL_NO_ERROR || extensionCount <= 0) {
        return false;
    }
    for (GLint i = 0; i < extensionCount; ++i) {
        const char* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (!name) continue;
        // multiview2 is the one that matters: plain GL_OVR_multiview cannot use
        // gl_ViewID_OVR outside of position, which is not enough for shading.
        if (std::strcmp(name, "GL_OVR_multiview2") == 0) {
            cached = 1;
            break;
        }
    }
    return cached != 0;
}

#if MODULARITY_HAS_OPENXR && MODULARITY_XR_HAS_GLES_TYPES

namespace {

// glFramebufferTextureMultiviewOVR is an extension entry point in both GL and
// GLES, so it is resolved at runtime rather than linked. Null when the extension
// is absent, which the caller treats as "no multiview".
using PfnFramebufferTextureMultiviewOVR = void(GL_APIENTRY*)(GLenum target,
                                                             GLenum attachment,
                                                             GLuint texture,
                                                             GLint level,
                                                             GLint baseViewIndex,
                                                             GLsizei numViews);

PfnFramebufferTextureMultiviewOVR resolveMultiviewEntryPoint() {
    static PfnFramebufferTextureMultiviewOVR fn = nullptr;
    static bool resolved = false;
    if (resolved) return fn;
    resolved = true;
    if (!XRMultiviewSupported()) return nullptr;
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    fn = reinterpret_cast<PfnFramebufferTextureMultiviewOVR>(
        eglGetProcAddress("glFramebufferTextureMultiviewOVR"));
#endif
    if (!fn) {
        XRLogWarn("GL_OVR_multiview2 is advertised but glFramebufferTextureMultiviewOVR "
                  "could not be resolved; falling back to Multi Pass.");
    }
    return fn;
}

// Ranked best-first. RGBA8 is preferred over SRGB8_ALPHA8 on purpose: Modularity's
// post-processing already writes display-ready values into an 8-bit target, so a
// runtime-side linear->sRGB conversion would gamma-correct twice and wash the
// image out. Picking the linear-storage format keeps XR output matching what the
// same scene looks like on a flat screen.
const int64_t kPreferredFormats[] = { kGlRgba8, kGlSrgb8Alpha8, kGlRgb10A2, kGlRgba16f };

bool chooseColorFormat(XrSession session, int64_t& outFormat, std::string& outError) {
    const XRSessionFunctions& fns = XRLoaderSessionFunctions();
    uint32_t count = 0;
    XrResult result = fns.EnumerateSwapchainFormats(session, 0, &count, nullptr);
    if (XR_FAILED(result) || count == 0) {
        XRLogResultFailure("xrEnumerateSwapchainFormats", static_cast<int32_t>(result));
        outError = "xrEnumerateSwapchainFormats reported no usable swapchain formats.";
        return false;
    }
    std::vector<int64_t> formats(count, 0);
    result = fns.EnumerateSwapchainFormats(session, count, &count, formats.data());
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrEnumerateSwapchainFormats", static_cast<int32_t>(result));
        outError = "xrEnumerateSwapchainFormats failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    for (int64_t preferred : kPreferredFormats) {
        if (std::find(formats.begin(), formats.end(), preferred) != formats.end()) {
            outFormat = preferred;
            if (preferred == kGlSrgb8Alpha8) {
                XRLogWarn("Runtime does not offer GL_RGBA8; using GL_SRGB8_ALPHA8, which "
                          "applies an extra sRGB encode on write.");
            }
            XRLogInfo("Swapchain format: " + XRSwapchainFormatName(outFormat));
            return true;
        }
    }

    // Nothing recognised. The runtime's first format is its own preference, so it
    // is a better guess than failing outright.
    outFormat = formats.front();
    XRLogWarn("No preferred swapchain format available; using the runtime's first choice (" +
              XRSwapchainFormatName(outFormat) + ").");
    return true;
}

} // namespace

XRSwapchainGL::~XRSwapchainGL() { destroy(); }

void XRSwapchainGL::moveFrom(XRSwapchainGL& other) {
    swapchain_ = other.swapchain_;
    images_ = std::move(other.images_);
    width_ = other.width_;
    height_ = other.height_;
    arraySize_ = other.arraySize_;
    colorFormat_ = other.colorFormat_;
    acquiredIndex_ = other.acquiredIndex_;

    other.swapchain_ = XR_NULL_HANDLE;
    other.images_.clear();
    other.acquiredIndex_ = -1;
}

bool XRSwapchainGL::create(XrSession session, int64_t colorFormat, uint32_t width,
                           uint32_t height, uint32_t arraySize, uint32_t sampleCount,
                           std::string& outError) {
    destroy();
    if (width == 0 || height == 0) {
        outError = "Refusing to create an OpenXR swapchain with a zero dimension.";
        return false;
    }

    XrSwapchainCreateInfo createInfo{};
    createInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                            XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    createInfo.format = colorFormat;
    createInfo.sampleCount = std::max(1u, sampleCount);
    createInfo.width = width;
    createInfo.height = height;
    createInfo.faceCount = 1;
    createInfo.arraySize = std::max(1u, arraySize);
    createInfo.mipCount = 1;

    const XrResult result =
        XRLoaderSessionFunctions().CreateSwapchain(session, &createInfo, &swapchain_);
    if (XR_FAILED(result) || swapchain_ == XR_NULL_HANDLE) {
        swapchain_ = XR_NULL_HANDLE;
        XRLogResultFailure("xrCreateSwapchain", static_cast<int32_t>(result));
        outError = "xrCreateSwapchain failed: " + XRResultString(static_cast<int32_t>(result));
        return false;
    }

    width_ = width;
    height_ = height;
    arraySize_ = std::max(1u, arraySize);
    colorFormat_ = colorFormat;

    if (!buildFramebuffers(outError)) {
        destroy();
        return false;
    }
    return true;
}

bool XRSwapchainGL::buildFramebuffers(std::string& outError) {
    const XRSessionFunctions& fns = XRLoaderSessionFunctions();

    uint32_t imageCount = 0;
    XrResult result = fns.EnumerateSwapchainImages(swapchain_, 0, &imageCount, nullptr);
    if (XR_FAILED(result) || imageCount == 0) {
        XRLogResultFailure("xrEnumerateSwapchainImages", static_cast<int32_t>(result));
        outError = "xrEnumerateSwapchainImages returned no images.";
        return false;
    }

    std::vector<XrSwapchainImageOpenGLESKHR> glImages(
        imageCount, XrSwapchainImageOpenGLESKHR{ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR, nullptr, 0 });
    result = fns.EnumerateSwapchainImages(
        swapchain_, imageCount, &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(glImages.data()));
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrEnumerateSwapchainImages", static_cast<int32_t>(result));
        outError = "xrEnumerateSwapchainImages failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    // Everything below rebinds GL state. Save what we disturb and put it back, so
    // creating a swapchain mid-session cannot leak a binding into the renderer.
    GLint previousFramebuffer = 0;
    GLint previousTexture2D = 0;
    GLint previousTextureArray = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previousTextureArray);

    const bool layered = arraySize_ > 1;
    PfnFramebufferTextureMultiviewOVR multiview = layered ? resolveMultiviewEntryPoint() : nullptr;
    if (layered && !multiview) {
        outError = "A multiview swapchain was created but glFramebufferTextureMultiviewOVR "
                   "is unavailable, so its layers cannot be attached.";
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        return false;
    }

    images_.clear();
    images_.reserve(imageCount);
    bool ok = true;
    for (uint32_t i = 0; i < imageCount && ok; ++i) {
        ImageTarget target;
        target.colorTexture = glImages[i].image;

        // Depth is ours: OpenXR only owns colour unless depth submission is on,
        // and the renderer needs a depth buffer to draw a 3D scene at all. One per
        // image rather than one shared, so a frame still in flight on the GPU
        // cannot have its depth overwritten by the next one.
        glGenTextures(1, &target.depthTexture);
        if (layered) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, target.depthTexture);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                         static_cast<GLsizei>(width_), static_cast<GLsizei>(height_),
                         static_cast<GLsizei>(arraySize_), 0, GL_DEPTH_COMPONENT,
                         GL_UNSIGNED_INT, nullptr);
        } else {
            glBindTexture(GL_TEXTURE_2D, target.depthTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                         static_cast<GLsizei>(width_), static_cast<GLsizei>(height_), 0,
                         GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        }

        glGenFramebuffers(1, &target.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        if (layered) {
            multiview(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target.colorTexture, 0, 0,
                      static_cast<GLsizei>(arraySize_));
            multiview(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target.depthTexture, 0, 0,
                      static_cast<GLsizei>(arraySize_));
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   target.colorTexture, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   target.depthTexture, 0);
        }

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%X", status);
            outError = std::string("OpenXR swapchain framebuffer is incomplete (") + buffer + ").";
            // Our own objects only. glImages[i].image belongs to the runtime.
            glDeleteFramebuffers(1, &target.framebuffer);
            glDeleteTextures(1, &target.depthTexture);
            ok = false;
            break;
        }
        images_.push_back(target);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture2D));
    glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(previousTextureArray));

    if (!ok) return false;
    XRLogInfo("Swapchain ready: " + std::to_string(width_) + "x" + std::to_string(height_) +
              ", " + std::to_string(arraySize_) + " layer(s), " +
              std::to_string(images_.size()) + " image(s)");
    return true;
}

bool XRSwapchainGL::acquireAndWait(uint32_t& outFramebuffer, std::string& outError) {
    outFramebuffer = 0;
    if (swapchain_ == XR_NULL_HANDLE || images_.empty()) {
        outError = "Cannot acquire from an uninitialized OpenXR swapchain.";
        return false;
    }
    if (acquiredIndex_ >= 0) {
        outError = "An OpenXR swapchain image is already acquired; release it first.";
        return false;
    }

    const XRSessionFunctions& fns = XRLoaderSessionFunctions();

    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    uint32_t index = 0;
    XrResult result = fns.AcquireSwapchainImage(swapchain_, &acquireInfo, &index);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrAcquireSwapchainImage", static_cast<int32_t>(result));
        outError = "xrAcquireSwapchainImage failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }
    if (index >= images_.size()) {
        outError = "xrAcquireSwapchainImage returned an out-of-range image index.";
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = fns.WaitSwapchainImage(swapchain_, &waitInfo);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrWaitSwapchainImage", static_cast<int32_t>(result));
        outError = "xrWaitSwapchainImage failed: " + XRResultString(static_cast<int32_t>(result));
        // The image is acquired even though the wait failed, so record it: the
        // caller's release() is what keeps the runtime's queue balanced.
        acquiredIndex_ = static_cast<int>(index);
        return false;
    }

    acquiredIndex_ = static_cast<int>(index);
    outFramebuffer = images_[index].framebuffer;
    return true;
}

bool XRSwapchainGL::release(std::string& outError) {
    if (acquiredIndex_ < 0) return true; // nothing held
    acquiredIndex_ = -1;

    XrSwapchainImageReleaseInfo releaseInfo{};
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    const XrResult result =
        XRLoaderSessionFunctions().ReleaseSwapchainImage(swapchain_, &releaseInfo);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrReleaseSwapchainImage", static_cast<int32_t>(result));
        outError = "xrReleaseSwapchainImage failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }
    return true;
}

void XRSwapchainGL::destroy() {
    // Ours to delete: framebuffers and depth. The colour textures are OpenXR's and
    // are freed by xrDestroySwapchain - deleting them here would corrupt the
    // runtime's own resource tracking.
    for (ImageTarget& target : images_) {
        if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
        if (target.depthTexture) glDeleteTextures(1, &target.depthTexture);
    }
    images_.clear();

    if (swapchain_ != XR_NULL_HANDLE) {
        const XRSessionFunctions& fns = XRLoaderSessionFunctions();
        if (fns.DestroySwapchain) fns.DestroySwapchain(swapchain_);
        swapchain_ = XR_NULL_HANDLE;
    }
    width_ = 0;
    height_ = 0;
    arraySize_ = 1;
    colorFormat_ = 0;
    acquiredIndex_ = -1;
}

bool XRSwapchainSetGL::create(XrSession session, const std::vector<XRViewInfo>& views,
                              XRRenderMode requestedMode, std::string& outError) {
    destroy();
    if (views.empty()) {
        outError = "Cannot create XR swapchains before the view configuration is known.";
        return false;
    }

    if (!chooseColorFormat(session, colorFormat_, outError)) return false;

    // Every stereo runtime in practice reports identical per-eye sizes, but the
    // spec does not promise it. Take the max so one swapchain size covers both
    // eyes rather than silently under-rendering one of them.
    viewCount_ = static_cast<uint32_t>(views.size());
    viewWidth_ = 0;
    viewHeight_ = 0;
    uint32_t sampleCount = 1;
    for (const XRViewInfo& view : views) {
        viewWidth_ = std::max(viewWidth_, view.recommendedWidth);
        viewHeight_ = std::max(viewHeight_, view.recommendedHeight);
        sampleCount = std::max(sampleCount, view.recommendedSampleCount);
    }
    if (viewWidth_ == 0 || viewHeight_ == 0) {
        outError = "Runtime reported a zero recommended eye resolution.";
        return false;
    }

    // Multiview only when it was asked for AND the driver really has it. This is
    // the fallback section 9 requires: a Single Pass setting must never turn into
    // an option that quietly does nothing.
    const bool wantsMultiview = (requestedMode == XRRenderMode::SinglePassMultiview);
    const bool canMultiview = wantsMultiview && viewCount_ == 2 && XRMultiviewSupported();
    multiviewFellBack_ = wantsMultiview && !canMultiview;
    resolvedMode_ = canMultiview ? XRRenderMode::SinglePassMultiview : XRRenderMode::MultiPass;
    if (multiviewFellBack_) {
        XRLogWarn("Single Pass / Multiview was requested but GL_OVR_multiview2 is not "
                  "available; falling back to Multi Pass.");
    }

    const size_t swapchainCount = canMultiview ? 1u : viewCount_;
    const uint32_t arraySize = canMultiview ? viewCount_ : 1u;
    swapchains_.resize(swapchainCount);
    for (size_t i = 0; i < swapchainCount; ++i) {
        if (!swapchains_[i].create(session, colorFormat_, viewWidth_, viewHeight_, arraySize,
                                   sampleCount, outError)) {
            destroy();
            return false;
        }
    }

    XRDiagnosticsSnapshot& diag = MutableDiagnostics();
    diag.swapchainFormat = XRSwapchainFormatName(colorFormat_);
    diag.multiviewSupported = XRMultiviewSupported();
    diag.activeRenderMode = ToString(resolvedMode_);
    diag.recommendedSwapchainSampleCount = sampleCount;

    valid_ = true;
    XRLogInfo(std::string("Render mode in use: ") + ToString(resolvedMode_));
    return true;
}

XRSwapchainGL* XRSwapchainSetGL::swapchainForView(uint32_t viewIndex) {
    if (swapchains_.empty()) return nullptr;
    if (resolvedMode_ == XRRenderMode::SinglePassMultiview) {
        // One array swapchain covers both eyes.
        return &swapchains_[0];
    }
    if (viewIndex >= swapchains_.size()) return nullptr;
    return &swapchains_[viewIndex];
}

void XRSwapchainSetGL::destroy() {
    swapchains_.clear();
    resolvedMode_ = XRRenderMode::MultiPass;
    multiviewFellBack_ = false;
    valid_ = false;
    viewCount_ = 0;
    viewWidth_ = 0;
    viewHeight_ = 0;
    colorFormat_ = 0;
}

#endif // MODULARITY_HAS_OPENXR && MODULARITY_XR_HAS_GLES_TYPES

} // namespace Modularity::XR
