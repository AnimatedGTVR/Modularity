#pragma once

#include "ScriptRuntime.h"
#include <cstdint>

extern "C" {
int modu_ctx_get_object_id(ScriptContext* ctx);
void modu_ctx_get_position(ScriptContext* ctx, float* x, float* y, float* z);
void modu_ctx_set_position(ScriptContext* ctx, float x, float y, float z);
void modu_ctx_get_rotation(ScriptContext* ctx, float* x, float* y, float* z);
void modu_ctx_set_rotation(ScriptContext* ctx, float x, float y, float z);
void modu_ctx_get_scale(ScriptContext* ctx, float* x, float* y, float* z);
void modu_ctx_set_scale(ScriptContext* ctx, float x, float y, float z);
int modu_ctx_has_rigidbody(ScriptContext* ctx);
int modu_ctx_ensure_rigidbody(ScriptContext* ctx, int useGravity, int kinematic);
int modu_ctx_set_rigidbody_velocity(ScriptContext* ctx, float x, float y, float z);
int modu_ctx_get_rigidbody_velocity(ScriptContext* ctx, float* x, float* y, float* z);
int modu_ctx_add_rigidbody_force(ScriptContext* ctx, float x, float y, float z);
int modu_ctx_add_rigidbody_impulse(ScriptContext* ctx, float x, float y, float z);
int modu_ctx_has_animation(ScriptContext* ctx);
int modu_ctx_play_animation(ScriptContext* ctx, int restart);
int modu_ctx_stop_animation(ScriptContext* ctx, int resetTime);
int modu_ctx_pause_animation(ScriptContext* ctx, int pause);
int modu_ctx_reverse_animation(ScriptContext* ctx, int restartIfStopped);
int modu_ctx_set_animation_time(ScriptContext* ctx, float timeSeconds);
float modu_ctx_get_animation_time(ScriptContext* ctx);
int modu_ctx_is_animation_playing(ScriptContext* ctx);
int modu_ctx_set_animation_loop(ScriptContext* ctx, int loop);
int modu_ctx_set_animation_play_speed(ScriptContext* ctx, float speed);
int modu_ctx_set_animation_play_on_awake(ScriptContext* ctx, int playOnAwake);
float modu_ctx_get_setting_float(ScriptContext* ctx, const char* key, float fallback);
int modu_ctx_get_setting_bool(ScriptContext* ctx, const char* key, int fallback);
void modu_ctx_get_setting_string(ScriptContext* ctx, const char* key, const char* fallback,
                                 char* outBuffer, int outBufferSize);
void modu_ctx_set_setting_float(ScriptContext* ctx, const char* key, float value);
void modu_ctx_set_setting_bool(ScriptContext* ctx, const char* key, int value);
void modu_ctx_set_setting_string(ScriptContext* ctx, const char* key, const char* value);
void modu_ctx_add_console_message(ScriptContext* ctx, const char* message, int type);
int modu_ctx_find_object_id_by_name(ScriptContext* ctx, const char* name);
void modu_ctx_get_object_name(ScriptContext* ctx, int id, char* outBuffer, int outBufferSize);
int modu_ctx_get_selected_object_id(ScriptContext* ctx);
int modu_ctx_get_scene_object_count(ScriptContext* ctx);
int modu_ctx_get_scene_object_id_at(ScriptContext* ctx, int index);
void modu_imgui_text(const char* text);
void modu_imgui_separator();
int modu_imgui_button(const char* label);
int modu_imgui_checkbox(const char* label, int* value);
int modu_imgui_drag_float(const char* label, float* value, float speed, float minValue, float maxValue);
int modu_imgui_drag_float3(const char* label, float* values, float speed, float minValue, float maxValue);
int modu_imgui_input_text(const char* label, char* buffer, int bufferSize);
int modu_imgui_begin_combo(const char* label, const char* previewValue);
void modu_imgui_end_combo();
int modu_imgui_selectable(const char* label, int selected);
int modu_imgui_accept_scene_object_drop(int* outId);
}

struct ManagedNativeApi {
    uint32_t version = 1;
    int (*getObjectId)(ScriptContext* ctx) = nullptr;
    void (*getPosition)(ScriptContext* ctx, float* x, float* y, float* z) = nullptr;
    void (*setPosition)(ScriptContext* ctx, float x, float y, float z) = nullptr;
    void (*getRotation)(ScriptContext* ctx, float* x, float* y, float* z) = nullptr;
    void (*setRotation)(ScriptContext* ctx, float x, float y, float z) = nullptr;
    void (*getScale)(ScriptContext* ctx, float* x, float* y, float* z) = nullptr;
    void (*setScale)(ScriptContext* ctx, float x, float y, float z) = nullptr;
    int (*hasRigidbody)(ScriptContext* ctx) = nullptr;
    int (*ensureRigidbody)(ScriptContext* ctx, int useGravity, int kinematic) = nullptr;
    int (*setRigidbodyVelocity)(ScriptContext* ctx, float x, float y, float z) = nullptr;
    int (*getRigidbodyVelocity)(ScriptContext* ctx, float* x, float* y, float* z) = nullptr;
    int (*addRigidbodyForce)(ScriptContext* ctx, float x, float y, float z) = nullptr;
    int (*addRigidbodyImpulse)(ScriptContext* ctx, float x, float y, float z) = nullptr;
    float (*getSettingFloat)(ScriptContext* ctx, const char* key, float fallback) = nullptr;
    int (*getSettingBool)(ScriptContext* ctx, const char* key, int fallback) = nullptr;
    void (*getSettingString)(ScriptContext* ctx, const char* key, const char* fallback,
                             char* outBuffer, int outBufferSize) = nullptr;
    void (*setSettingFloat)(ScriptContext* ctx, const char* key, float value) = nullptr;
    void (*setSettingBool)(ScriptContext* ctx, const char* key, int value) = nullptr;
    void (*setSettingString)(ScriptContext* ctx, const char* key, const char* value) = nullptr;
    void (*addConsoleMessage)(ScriptContext* ctx, const char* message, int type) = nullptr;
    int (*findObjectIdByName)(ScriptContext* ctx, const char* name) = nullptr;
    void (*getObjectName)(ScriptContext* ctx, int id, char* outBuffer, int outBufferSize) = nullptr;
    int (*getSelectedObjectId)(ScriptContext* ctx) = nullptr;
    void (*imguiText)(const char* text) = nullptr;
    void (*imguiSeparator)() = nullptr;
    int (*imguiButton)(const char* label) = nullptr;
    int (*imguiCheckbox)(const char* label, int* value) = nullptr;
    int (*imguiDragFloat)(const char* label, float* value, float speed, float minValue, float maxValue) = nullptr;
    int (*imguiDragFloat3)(const char* label, float* values, float speed, float minValue, float maxValue) = nullptr;
    int (*imguiInputText)(const char* label, char* buffer, int bufferSize) = nullptr;
    int (*imguiAcceptSceneObjectDrop)(int* outId) = nullptr;
    // Version 4+ additions appended to preserve ABI layout for older managed assemblies.
    int (*getSceneObjectCount)(ScriptContext* ctx) = nullptr;
    int (*getSceneObjectIdAt)(ScriptContext* ctx, int index) = nullptr;
    int (*imguiBeginCombo)(const char* label, const char* previewValue) = nullptr;
    void (*imguiEndCombo)() = nullptr;
    int (*imguiSelectable)(const char* label, int selected) = nullptr;
    // Version 5+ additions.
    int (*hasAnimation)(ScriptContext* ctx) = nullptr;
    int (*playAnimation)(ScriptContext* ctx, int restart) = nullptr;
    int (*stopAnimation)(ScriptContext* ctx, int resetTime) = nullptr;
    int (*pauseAnimation)(ScriptContext* ctx, int pause) = nullptr;
    int (*reverseAnimation)(ScriptContext* ctx, int restartIfStopped) = nullptr;
    int (*setAnimationTime)(ScriptContext* ctx, float timeSeconds) = nullptr;
    float (*getAnimationTime)(ScriptContext* ctx) = nullptr;
    int (*isAnimationPlaying)(ScriptContext* ctx) = nullptr;
    int (*setAnimationLoop)(ScriptContext* ctx, int loop) = nullptr;
    int (*setAnimationPlaySpeed)(ScriptContext* ctx, float speed) = nullptr;
    int (*setAnimationPlayOnAwake)(ScriptContext* ctx, int playOnAwake) = nullptr;
};

ManagedNativeApi BuildManagedNativeApi();
