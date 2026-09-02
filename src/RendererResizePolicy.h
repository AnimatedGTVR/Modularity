#pragma once

// Pure decision logic for Renderer::resize.
//
// Split out of Rendering.h so it carries no OpenGL, GLFW or engine dependency
// and can be exercised headlessly (tools/resize-selftest). The policy itself is
// cross-platform defensive validation: a minimized or mid-restore window reports
// a 0x0 client area on Windows, and such a size must never reach glTexImage2D or
// glRenderbufferStorage.

namespace ModuRenderer {

struct ResizeRequest {
    int width = 0;
    int height = 0;
    int currentWidth = 0;
    int currentHeight = 0;
    // Renderer::initialize() has run and the FBO/texture/RBO objects exist.
    bool initialized = false;
    // A resize is already on the stack for this renderer.
    bool resizeInProgress = false;
    bool shuttingDown = false;
};

enum class ResizeDecision {
    Accept,
    RejectInvalidSize,     // zero or negative in either axis
    RejectShuttingDown,
    RejectReentrant,
    RejectNotInitialized,
    RejectUnchanged,
};

// Rejecting deliberately leaves the caller's currentWidth/currentHeight alone, so
// the last known-good size survives and a later valid request still reallocates.
inline ResizeDecision EvaluateResizeRequest(const ResizeRequest& request) {
    if (request.width <= 0 || request.height <= 0) {
        return ResizeDecision::RejectInvalidSize;
    }
    if (request.shuttingDown) {
        return ResizeDecision::RejectShuttingDown;
    }
    if (request.resizeInProgress) {
        return ResizeDecision::RejectReentrant;
    }
    if (!request.initialized) {
        return ResizeDecision::RejectNotInitialized;
    }
    if (request.width == request.currentWidth && request.height == request.currentHeight) {
        return ResizeDecision::RejectUnchanged;
    }
    return ResizeDecision::Accept;
}

inline const char* ResizeDecisionName(ResizeDecision decision) {
    switch (decision) {
        case ResizeDecision::Accept:               return "accept";
        case ResizeDecision::RejectInvalidSize:    return "reject:invalid-size";
        case ResizeDecision::RejectShuttingDown:   return "reject:shutting-down";
        case ResizeDecision::RejectReentrant:      return "reject:reentrant";
        case ResizeDecision::RejectNotInitialized: return "reject:not-initialized";
        case ResizeDecision::RejectUnchanged:      return "reject:unchanged";
    }
    return "reject:unknown";
}

// Clamp to what the driver will accept. Exceeding GL_MAX_TEXTURE_SIZE or
// GL_MAX_RENDERBUFFER_SIZE leaves an incomplete framebuffer, and some drivers
// fault rather than reporting an error. A non-positive limit means the query
// failed, so fall back to a conservative bound.
inline void ClampResizeToLimit(int& width, int& height, int maxDimension) {
    const int limit = (maxDimension > 0) ? maxDimension : 16384;
    if (width > limit) width = limit;
    if (height > limit) height = limit;
}

}  // namespace ModuRenderer
