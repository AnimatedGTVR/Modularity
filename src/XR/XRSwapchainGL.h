#pragma once

// OpenXR swapchain images wrapped as Modularity render targets.
//
// This is the whole "OpenXR owns the images, Modularity owns the drawing" seam:
//
//     OpenXR swapchain image  ->  GL ES texture  ->  our framebuffer  ->  renderer
//
// OpenXR hands back GL texture names it owns. We create one framebuffer per image
// that attaches that texture as colour, plus a depth attachment of our own (the
// runtime only owns colour unless depth submission is on). The renderer then draws
// into that framebuffer exactly the way it draws into a preview target.
//
// Two ownership rules that are not negotiable (section 41):
//   * The colour textures belong to OpenXR. They are never glDeleteTextures'd
//     here, and they stop being valid the moment the swapchain is destroyed.
//   * The framebuffers and depth textures belong to us, and are the only things
//     released in destroy().
//
// Render-mode handling lives in XRSwapchainSetGL: one array swapchain for
// Single Pass / Multiview, or one swapchain per eye for Multi Pass. The decision
// is made once at creation from what the GL driver actually reports, never
// assumed, so an unsupported multiview request becomes Multi Pass instead of
// rendering nothing.

#include "XRRuntime.h"
#include "XRSettings.h"

#include <cstdint>
#include <string>
#include <vector>

#if MODULARITY_HAS_OPENXR
#include "XRPlatform.h"
#endif

namespace Modularity::XR {

// True when the GL context that is current right now exposes GL_OVR_multiview2,
// which is what Single Pass rendering needs. Must be called with a context
// current; caches its answer for the life of the process, so it is cheap to ask
// repeatedly but must not be called before the renderer is up.
bool XRMultiviewSupported();

// Human-readable name for a GL internal format, for diagnostics. Falls back to a
// hex value for formats not in the table rather than claiming "unknown".
std::string XRSwapchainFormatName(int64_t format);

#if MODULARITY_HAS_OPENXR && MODULARITY_XR_HAS_GLES_TYPES

// One OpenXR swapchain plus the framebuffers wrapping its images.
class XRSwapchainGL {
public:
    XRSwapchainGL() = default;
    ~XRSwapchainGL();

    XRSwapchainGL(const XRSwapchainGL&) = delete;
    XRSwapchainGL& operator=(const XRSwapchainGL&) = delete;
    XRSwapchainGL(XRSwapchainGL&& other) noexcept { moveFrom(other); }
    XRSwapchainGL& operator=(XRSwapchainGL&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

    // `arraySize` is 2 for a multiview swapchain, 1 for a per-eye one.
    bool create(XrSession session, int64_t colorFormat, uint32_t width, uint32_t height,
                uint32_t arraySize, uint32_t sampleCount, std::string& outError);
    void destroy();

    bool isValid() const { return swapchain_ != XR_NULL_HANDLE; }

    // xrAcquireSwapchainImage + xrWaitSwapchainImage. On success `outFramebuffer`
    // is a complete FBO whose colour attachment is the acquired image, ready to
    // be bound. Must be paired with release() in the same frame.
    bool acquireAndWait(uint32_t& outFramebuffer, std::string& outError);

    // xrReleaseSwapchainImage. A no-op when nothing is acquired, so an error path
    // between acquire and release cannot desynchronize the runtime.
    bool release(std::string& outError);

    XrSwapchain handle() const { return swapchain_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t arraySize() const { return arraySize_; }
    int64_t colorFormat() const { return colorFormat_; }

private:
    void moveFrom(XRSwapchainGL& other);
    bool buildFramebuffers(std::string& outError);

    struct ImageTarget {
        uint32_t colorTexture = 0;  // OpenXR's. Not ours to delete.
        uint32_t depthTexture = 0;  // ours
        uint32_t framebuffer = 0;   // ours
    };

    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    std::vector<ImageTarget> images_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t arraySize_ = 1;
    int64_t colorFormat_ = 0;
    // Index returned by the last successful acquire, or -1 when nothing is held.
    int acquiredIndex_ = -1;
};

// The per-view arrangement for one XR session: either a single 2-layer array
// swapchain (Single Pass / Multiview) or one swapchain per eye (Multi Pass).
class XRSwapchainSetGL {
public:
    // `views` comes from the runtime's view configuration (2 entries for stereo).
    // `requestedMode` is the project setting; the mode actually used is reported
    // by resolvedRenderMode() and may differ when the driver lacks multiview.
    bool create(XrSession session, const std::vector<XRViewInfo>& views,
                XRRenderMode requestedMode, std::string& outError);
    void destroy();

    bool isValid() const { return valid_; }
    XRRenderMode resolvedRenderMode() const { return resolvedMode_; }
    // True when Single Pass was asked for but the driver could not provide it.
    bool multiviewFellBack() const { return multiviewFellBack_; }

    // Multiview: one swapchain shared by both eyes, so every view maps to index 0.
    // Multi Pass: one per view. Returns nullptr for an out-of-range view.
    XRSwapchainGL* swapchainForView(uint32_t viewIndex);
    size_t swapchainCount() const { return swapchains_.size(); }

    uint32_t viewWidth() const { return viewWidth_; }
    uint32_t viewHeight() const { return viewHeight_; }
    int64_t colorFormat() const { return colorFormat_; }

private:
    std::vector<XRSwapchainGL> swapchains_;
    XRRenderMode resolvedMode_ = XRRenderMode::MultiPass;
    bool multiviewFellBack_ = false;
    bool valid_ = false;
    uint32_t viewCount_ = 0;
    uint32_t viewWidth_ = 0;
    uint32_t viewHeight_ = 0;
    int64_t colorFormat_ = 0;
};

#endif // MODULARITY_HAS_OPENXR && MODULARITY_XR_HAS_GLES_TYPES

} // namespace Modularity::XR
