#pragma once
#include "Common.h"
// Unified editor drag/drop ghost preview.
// Usage at a drag source:
//     if (DragPreview::BeginSource(flags)) {
//         ImGui::SetDragDropPayload(type, data, size);
//         DragPreview::SubmitMeta(displayName, icon, type);
//         DragPreview::EndSource();
//     }
//
// This is Once per frame, after panels have submitted their sources, but before ImGui::Render(), call DragPreview::UpdateAndRender().
namespace DragPreview {
    struct Settings {
        bool  enabled = true;
        float followStiffness       = 280.0f;
        float followDamping         = 22.0f;
        float cursorOffsetX         = 0.0f;
        float cursorOffsetY         = 10.0f;

        // Rotation pendulum-style.
        float rotationStiffness     = 70.0f;
        float rotationDamping       = 7.5f;
        float rotationDrive         = 0.018f;  // degrees of tilt per (px/s) of lateral vel
        float maxRotationDeg        = 14.0f;

        // Scale response to motion.
        float scaleResponse         = 0.08f;
        float scaleSmoothing        = 11.0f;

        // Pop-in transient (when a drag begins the card fades+scales into place)
        float popInStartScale       = 0.55f;
        float popInRiseDistance     = 14.0f;
        float popInFadeRate         = 9.0f;

        // Idle micro-float ("dreamy" drift)
        float floatAmplitude        = 1.4f;    // px
        float floatFrequency        = 2.4f;    // Hz

        // Drop animation
        float dropFallDistance      = 90.0f;
        float dropFadeDuration      = 0.50f;   // seconds
        float dropExtraRotationDeg  = 22.0f;   // additional tilt during fall
        float dropEndScale          = 0.82f;
        float dropGravity           = 220.0f;  // px / s^2

        // Visual style (matches editor's dark slate theme)
        float opacity               = 0.85f;
        float iconSize              = 18.0f;
        float cardPaddingX          = 10.0f;
        float cardPaddingY          = 6.0f;
        float iconGap               = 6.0f;
        float shadowOffset          = 4.0f;
        ImU32 cardColor             = IM_COL32( 34,  36,  54, 240);
        ImU32 cardBorderColor       = IM_COL32(110, 124, 196, 220);
        ImU32 cardShadowColor       = IM_COL32(  0,   0,   0, 110);
        ImU32 textColor             = IM_COL32(236, 238, 250, 255);
    };

    Settings& GetSettings();
    bool BeginSource(ImGuiDragDropFlags userFlags = 0);
    void EndSource();
    void SubmitMeta(const char* displayName, ImTextureID icon = (ImTextureID)0, const char* payloadType = nullptr); // Inside an active BeginSource() block, after ImGui::SetDragDropPayload(). When the floating preview is enabled, registers the metadata used to render the ghost card. When disabled, falls back to ImGui::Text() inside the native drag tooltip so source descriptions remain visible.
    void UpdateAndRender();                                                                                         // Per-frame update + draw. Must be invoked once between panel rendering and ImGui::Render() while editor UI is being drawn.
    void Cancel();
}
