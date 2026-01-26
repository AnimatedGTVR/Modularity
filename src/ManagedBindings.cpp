#include "ManagedBindings.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

int modu_ctx_get_object_id(ScriptContext* ctx) {
    return (ctx && ctx->object) ? ctx->object->id : -1;
}

void modu_ctx_get_position(ScriptContext* ctx, float* x, float* y, float* z) {
    if (!ctx || !ctx->object || !x || !y || !z) return;
    *x = ctx->object->position.x;
    *y = ctx->object->position.y;
    *z = ctx->object->position.z;
}

void modu_ctx_set_position(ScriptContext* ctx, float x, float y, float z) {
    if (!ctx) return;
    ctx->SetPosition(glm::vec3(x, y, z));
}

void modu_ctx_get_rotation(ScriptContext* ctx, float* x, float* y, float* z) {
    if (!ctx || !ctx->object || !x || !y || !z) return;
    *x = ctx->object->rotation.x;
    *y = ctx->object->rotation.y;
    *z = ctx->object->rotation.z;
}

void modu_ctx_set_rotation(ScriptContext* ctx, float x, float y, float z) {
    if (!ctx) return;
    ctx->SetRotation(glm::vec3(x, y, z));
}

void modu_ctx_get_scale(ScriptContext* ctx, float* x, float* y, float* z) {
    if (!ctx || !ctx->object || !x || !y || !z) return;
    *x = ctx->object->scale.x;
    *y = ctx->object->scale.y;
    *z = ctx->object->scale.z;
}

void modu_ctx_set_scale(ScriptContext* ctx, float x, float y, float z) {
    if (!ctx) return;
    ctx->SetScale(glm::vec3(x, y, z));
}

int modu_ctx_has_rigidbody(ScriptContext* ctx) {
    return (ctx && ctx->HasRigidbody()) ? 1 : 0;
}

int modu_ctx_ensure_rigidbody(ScriptContext* ctx, int useGravity, int kinematic) {
    if (!ctx) return 0;
    return ctx->EnsureRigidbody(useGravity != 0, kinematic != 0) ? 1 : 0;
}

int modu_ctx_set_rigidbody_velocity(ScriptContext* ctx, float x, float y, float z) {
    if (!ctx) return 0;
    return ctx->SetRigidbodyVelocity(glm::vec3(x, y, z)) ? 1 : 0;
}

int modu_ctx_get_rigidbody_velocity(ScriptContext* ctx, float* x, float* y, float* z) {
    if (!ctx || !x || !y || !z) return 0;
    glm::vec3 velocity(0.0f);
    if (!ctx->GetRigidbodyVelocity(velocity)) return 0;
    *x = velocity.x;
    *y = velocity.y;
    *z = velocity.z;
    return 1;
}

int modu_ctx_add_rigidbody_force(ScriptContext* ctx, float x, float y, float z) {
    if (!ctx) return 0;
    return ctx->AddRigidbodyForce(glm::vec3(x, y, z)) ? 1 : 0;
}

int modu_ctx_add_rigidbody_impulse(ScriptContext* ctx, float x, float y, float z) {
    if (!ctx) return 0;
    return ctx->AddRigidbodyImpulse(glm::vec3(x, y, z)) ? 1 : 0;
}

float modu_ctx_get_setting_float(ScriptContext* ctx, const char* key, float fallback) {
    if (!ctx || !key) return fallback;
    return ctx->GetSettingFloat(key, fallback);
}

int modu_ctx_get_setting_bool(ScriptContext* ctx, const char* key, int fallback) {
    if (!ctx || !key) return fallback ? 1 : 0;
    return ctx->GetSettingBool(key, fallback != 0) ? 1 : 0;
}

void modu_ctx_get_setting_string(ScriptContext* ctx, const char* key, const char* fallback,
                                 char* outBuffer, int outBufferSize) {
    if (!outBuffer || outBufferSize <= 0) return;
    std::string value;
    if (!ctx || !key) {
        value = fallback ? fallback : "";
    } else {
        value = ctx->GetSetting(key, fallback ? fallback : "");
    }
    std::snprintf(outBuffer, static_cast<size_t>(outBufferSize), "%s", value.c_str());
}

void modu_ctx_set_setting_float(ScriptContext* ctx, const char* key, float value) {
    if (!ctx || !key) return;
    ctx->SetSettingFloat(key, value);
}

void modu_ctx_set_setting_bool(ScriptContext* ctx, const char* key, int value) {
    if (!ctx || !key) return;
    ctx->SetSettingBool(key, value != 0);
}

void modu_ctx_set_setting_string(ScriptContext* ctx, const char* key, const char* value) {
    if (!ctx || !key) return;
    ctx->SetSetting(key, value ? value : "");
}

void modu_ctx_add_console_message(ScriptContext* ctx, const char* message, int type) {
    if (!ctx || !message) return;
    ctx->AddConsoleMessage(message, static_cast<ConsoleMessageType>(type));
}

int modu_ctx_find_object_id_by_name(ScriptContext* ctx, const char* name) {
    if (!ctx || !name) return -1;
    SceneObject* obj = ctx->FindObjectByName(name);
    return obj ? obj->id : -1;
}

void modu_ctx_get_object_name(ScriptContext* ctx, int id, char* outBuffer, int outBufferSize) {
    if (!outBuffer || outBufferSize <= 0) return;
    const char* name = "";
    if (ctx) {
        SceneObject* obj = ctx->FindObjectById(id);
        if (obj) name = obj->name.c_str();
    }
    std::snprintf(outBuffer, static_cast<size_t>(outBufferSize), "%s", name);
}

int modu_ctx_get_selected_object_id(ScriptContext* ctx) {
    if (!ctx) return -1;
    return ctx->GetSelectedObjectId();
}

void modu_imgui_text(const char* text) {
    if (!text) return;
    ImGui::TextUnformatted(text);
}

void modu_imgui_separator() {
    ImGui::Separator();
}

int modu_imgui_button(const char* label) {
    if (!label) return 0;
    return ImGui::Button(label) ? 1 : 0;
}

int modu_imgui_checkbox(const char* label, int* value) {
    if (!label || !value) return 0;
    bool checked = (*value != 0);
    bool changed = ImGui::Checkbox(label, &checked);
    *value = checked ? 1 : 0;
    return changed ? 1 : 0;
}

int modu_imgui_drag_float(const char* label, float* value, float speed, float minValue, float maxValue) {
    if (!label || !value) return 0;
    return ImGui::DragFloat(label, value, speed, minValue, maxValue) ? 1 : 0;
}

int modu_imgui_drag_float3(const char* label, float* values, float speed, float minValue, float maxValue) {
    if (!label || !values) return 0;
    return ImGui::DragFloat3(label, values, speed, minValue, maxValue) ? 1 : 0;
}

int modu_imgui_input_text(const char* label, char* buffer, int bufferSize) {
    if (!label || !buffer || bufferSize <= 0) return 0;
    return ImGui::InputText(label, buffer, static_cast<size_t>(bufferSize)) ? 1 : 0;
}

int modu_imgui_accept_scene_object_drop(int* outId) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            if (outId && payload->DataSize == sizeof(int)) {
                *outId = *(const int*)payload->Data;
            }
            ImGui::EndDragDropTarget();
            return 1;
        }
        ImGui::EndDragDropTarget();
    }
    return 0;
}

ManagedNativeApi BuildManagedNativeApi() {
    ManagedNativeApi api;
    api.version = 3;
    api.getObjectId = modu_ctx_get_object_id;
    api.getPosition = modu_ctx_get_position;
    api.setPosition = modu_ctx_set_position;
    api.getRotation = modu_ctx_get_rotation;
    api.setRotation = modu_ctx_set_rotation;
    api.getScale = modu_ctx_get_scale;
    api.setScale = modu_ctx_set_scale;
    api.hasRigidbody = modu_ctx_has_rigidbody;
    api.ensureRigidbody = modu_ctx_ensure_rigidbody;
    api.setRigidbodyVelocity = modu_ctx_set_rigidbody_velocity;
    api.getRigidbodyVelocity = modu_ctx_get_rigidbody_velocity;
    api.addRigidbodyForce = modu_ctx_add_rigidbody_force;
    api.addRigidbodyImpulse = modu_ctx_add_rigidbody_impulse;
    api.getSettingFloat = modu_ctx_get_setting_float;
    api.getSettingBool = modu_ctx_get_setting_bool;
    api.getSettingString = modu_ctx_get_setting_string;
    api.setSettingFloat = modu_ctx_set_setting_float;
    api.setSettingBool = modu_ctx_set_setting_bool;
    api.setSettingString = modu_ctx_set_setting_string;
    api.addConsoleMessage = modu_ctx_add_console_message;
    api.findObjectIdByName = modu_ctx_find_object_id_by_name;
    api.getObjectName = modu_ctx_get_object_name;
    api.getSelectedObjectId = modu_ctx_get_selected_object_id;
    api.imguiText = modu_imgui_text;
    api.imguiSeparator = modu_imgui_separator;
    api.imguiButton = modu_imgui_button;
    api.imguiCheckbox = modu_imgui_checkbox;
    api.imguiDragFloat = modu_imgui_drag_float;
    api.imguiDragFloat3 = modu_imgui_drag_float3;
    api.imguiInputText = modu_imgui_input_text;
    api.imguiAcceptSceneObjectDrop = modu_imgui_accept_scene_object_drop;
    return api;
}
