#include "ScriptRuntimeCAPI.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ControllerSettings {
    ModuVec3 moveTuning;
    ModuVec3 lookTuning;
    ModuVec3 capsuleTuning;
    ModuVec3 gravityTuning;
    int enableMouseLook;
    int requireMouseButton;
    int enforceCollider;
    int enforceRigidbody;
} ControllerSettings;

typedef struct ControllerState {
    int objectId;
    int initialized;
    int warnedMissingRigidbody;
    float pitch;
    float yaw;
    float verticalVelocity;
} ControllerState;

static ControllerSettings g_settings = {
    {5.5f, 7.5f, 6.5f},
    {0.12f, 200.0f, 0.0f},
    {1.8f, 0.4f, 0.2f},
    {-9.81f, 0.4f, 30.0f},
    1,
    0,
    1,
    1
};

static int g_settingsLoaded = 0;

static ControllerState* g_states = NULL;
static int g_stateCount = 0;
static int g_stateCapacity = 0;

static float clampf32(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static int ensureStateCapacity(int minCapacity) {
    if (g_stateCapacity >= minCapacity) return 1;

    int nextCapacity = (g_stateCapacity > 0) ? g_stateCapacity * 2 : 16;
    while (nextCapacity < minCapacity) {
        nextCapacity *= 2;
    }

    ControllerState* nextStates =
        (ControllerState*)realloc(g_states, (size_t)nextCapacity * sizeof(ControllerState));
    if (!nextStates) return 0;

    g_states = nextStates;
    g_stateCapacity = nextCapacity;
    return 1;
}

static ControllerState* getState(int objectId) {
    for (int i = 0; i < g_stateCount; ++i) {
        if (g_states[i].objectId == objectId) {
            return &g_states[i];
        }
    }

    if (!ensureStateCapacity(g_stateCount + 1)) return NULL;
    ControllerState* state = &g_states[g_stateCount++];
    memset(state, 0, sizeof(*state));
    state->objectId = objectId;
    return state;
}

static void skipSeparators(const char** cursor) {
    while (**cursor == ' ' || **cursor == '\t' || **cursor == ',') {
        ++(*cursor);
    }
}

static int parseVec3(const char* text, ModuVec3* out) {
    if (!text || !out) return 0;

    const char* cursor = text;
    char* end = NULL;
    float values[3] = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 3; ++i) {
        skipSeparators(&cursor);
        values[i] = strtof(cursor, &end);
        if (end == cursor) return 0;
        cursor = end;
    }

    out->x = values[0];
    out->y = values[1];
    out->z = values[2];
    return 1;
}

static void formatVec3(ModuVec3 value, char* outBuffer, int outBufferSize) {
    if (!outBuffer || outBufferSize <= 0) return;
    snprintf(outBuffer, (size_t)outBufferSize, "%.6g,%.6g,%.6g", value.x, value.y, value.z);
}

static ModuVec3 readVec3Setting(ModuScriptContext* ctx, const char* key, ModuVec3 fallback) {
    char fallbackBuffer[96];
    char valueBuffer[96];
    ModuVec3 out = fallback;

    formatVec3(fallback, fallbackBuffer, (int)sizeof(fallbackBuffer));
    if (!Modu_GetSettingString(ctx, key, fallbackBuffer, valueBuffer, (int)sizeof(valueBuffer))) {
        return out;
    }

    if (!parseVec3(valueBuffer, &out)) {
        return fallback;
    }
    return out;
}

static void writeVec3Setting(ModuScriptContext* ctx, const char* key, ModuVec3 value) {
    char buffer[96];
    formatVec3(value, buffer, (int)sizeof(buffer));
    Modu_SetSettingString(ctx, key, buffer);
}

static void bindSettings(ModuScriptContext* ctx, ControllerSettings* settings) {
    settings->moveTuning = readVec3Setting(ctx, "moveTuning", settings->moveTuning);
    settings->lookTuning = readVec3Setting(ctx, "lookTuning", settings->lookTuning);
    settings->capsuleTuning = readVec3Setting(ctx, "capsuleTuning", settings->capsuleTuning);
    settings->gravityTuning = readVec3Setting(ctx, "gravityTuning", settings->gravityTuning);

    settings->enableMouseLook =
        Modu_GetSettingBool(ctx, "enableMouseLook", settings->enableMouseLook ? 1 : 0);
    settings->requireMouseButton =
        Modu_GetSettingBool(ctx, "requireMouseButton", settings->requireMouseButton ? 1 : 0);
    settings->enforceCollider =
        Modu_GetSettingBool(ctx, "enforceCollider", settings->enforceCollider ? 1 : 0);
    settings->enforceRigidbody =
        Modu_GetSettingBool(ctx, "enforceRigidbody", settings->enforceRigidbody ? 1 : 0);
}

static void saveSettings(ModuScriptContext* ctx, const ControllerSettings* settings) {
    writeVec3Setting(ctx, "moveTuning", settings->moveTuning);
    writeVec3Setting(ctx, "lookTuning", settings->lookTuning);
    writeVec3Setting(ctx, "capsuleTuning", settings->capsuleTuning);
    writeVec3Setting(ctx, "gravityTuning", settings->gravityTuning);
    Modu_SetSettingBool(ctx, "enableMouseLook", settings->enableMouseLook ? 1 : 0);
    Modu_SetSettingBool(ctx, "requireMouseButton", settings->requireMouseButton ? 1 : 0);
    Modu_SetSettingBool(ctx, "enforceCollider", settings->enforceCollider ? 1 : 0);
    Modu_SetSettingBool(ctx, "enforceRigidbody", settings->enforceRigidbody ? 1 : 0);
}

void Modu_OnInspector(ModuScriptContext* ctx) {
    if (!ctx) return;

    bindSettings(ctx, &g_settings);
    g_settingsLoaded = 1;

    int changed = 0;
    Modu_InspectorText(ctx, "Standalone Movement Controller");
    Modu_InspectorSeparator(ctx);
    changed |= Modu_InspectorDragFloat3(ctx, "Walk/Run/Jump", &g_settings.moveTuning.x, 0.05f,
                                        0.0f, 25.0f, "%.2f");
    changed |= Modu_InspectorDragFloat2(ctx, "Look Sens/Clamp", &g_settings.lookTuning.x, 0.01f,
                                        0.0f, 500.0f, "%.2f");
    changed |= Modu_InspectorDragFloat3(ctx, "Height/Radius/Snap", &g_settings.capsuleTuning.x, 0.02f,
                                        0.0f, 5.0f, "%.2f");
    changed |= Modu_InspectorDragFloat3(ctx, "Gravity/Probe/MaxFall", &g_settings.gravityTuning.x, 0.05f,
                                        -50.0f, 50.0f, "%.2f");
    changed |= Modu_InspectorCheckbox(ctx, "Enable Mouse Look", &g_settings.enableMouseLook);
    changed |= Modu_InspectorCheckbox(ctx, "Hold RMB to Look", &g_settings.requireMouseButton);
    changed |= Modu_InspectorCheckbox(ctx, "Force Collider", &g_settings.enforceCollider);
    changed |= Modu_InspectorCheckbox(ctx, "Force Rigidbody", &g_settings.enforceRigidbody);

    if (changed) {
        saveSettings(ctx, &g_settings);
    }
}

void Modu_Begin(ModuScriptContext* ctx) {
    if (!ctx) return;

    int objectId = Modu_GetObjectId(ctx);
    if (objectId < 0) return;

    if (!g_settingsLoaded) {
        bindSettings(ctx, &g_settings);
        g_settingsLoaded = 1;
    }
    ControllerState* state = getState(objectId);
    if (!state) return;

    ModuVec3 rot = Modu_GetRotation(ctx);
    state->pitch = rot.x;
    state->yaw = rot.y;
    state->verticalVelocity = 0.0f;
    state->initialized = 1;

    if (g_settings.enforceCollider) {
        Modu_EnsureCapsuleCollider(ctx, g_settings.capsuleTuning.x, g_settings.capsuleTuning.y);
    }
    if (g_settings.enforceRigidbody) {
        Modu_EnsureRigidbody(ctx, 1, 0);
    }
}

void Modu_TickUpdate(ModuScriptContext* ctx, float dt) {
    if (!ctx || dt <= 0.0f) return;

    int objectId = Modu_GetObjectId(ctx);
    if (objectId < 0) return;

    if (!g_settingsLoaded) {
        bindSettings(ctx, &g_settings);
        g_settingsLoaded = 1;
    }
    ControllerState* state = getState(objectId);
    if (!state) return;

    if (!state->initialized) {
        ModuVec3 rot = Modu_GetRotation(ctx);
        state->pitch = rot.x;
        state->yaw = rot.y;
        state->verticalVelocity = 0.0f;
        state->initialized = 1;
    }

    if (g_settings.enableMouseLook) {
        float maxMouseDelta = fmaxf(5.0f, g_settings.lookTuning.y);
        Modu_ApplyMouseLook(ctx, &state->pitch, &state->yaw, g_settings.lookTuning.x,
                            maxMouseDelta, dt, g_settings.requireMouseButton);
    }

    ModuVec3 move = Modu_GetMoveInputWASD(ctx, state->pitch, state->yaw);
    float targetSpeed = Modu_IsSprintDown(ctx) ? g_settings.moveTuning.y : g_settings.moveTuning.x;
    ModuVec3 velocity = {move.x * targetSpeed, 0.0f, move.z * targetSpeed};

    float capsuleHalf = fmaxf(0.1f, g_settings.capsuleTuning.x * 0.5f);
    float groundSnap = g_settings.capsuleTuning.z;
    float gravity = g_settings.gravityTuning.x;
    float maxFall = fmaxf(1.0f, g_settings.gravityTuning.z);

    ModuVec3 physVel = {0.0f, 0.0f, 0.0f};
    int havePhysVel = Modu_GetRigidbodyVelocity(ctx, &physVel);
    if (havePhysVel) {
        state->verticalVelocity = physVel.y;
    }

    /* Grounding fallback without a raycast C helper yet. */
    ModuVec3 pos = Modu_GetPosition(ctx);
    int grounded = (pos.y <= capsuleHalf + groundSnap) && (state->verticalVelocity <= 0.35f);
    if (grounded) {
        state->verticalVelocity = 0.0f;
        if (!havePhysVel && pos.y < capsuleHalf) {
            pos.y = capsuleHalf;
            Modu_SetPosition(ctx, pos);
        }
        if (Modu_IsJumpDown(ctx)) {
            state->verticalVelocity = g_settings.moveTuning.z;
        }
    } else {
        state->verticalVelocity += gravity * dt;
    }

    state->verticalVelocity = clampf32(state->verticalVelocity, -maxFall, maxFall);
    velocity.y = state->verticalVelocity;

    ModuVec3 rotation = {state->pitch, state->yaw, 0.0f};
    if (!Modu_SetRigidbodyRotation(ctx, rotation)) {
        Modu_SetRotation(ctx, rotation);
    }

    if (!Modu_SetRigidbodyVelocity(ctx, velocity)) {
        if (g_settings.enforceRigidbody && !state->warnedMissingRigidbody) {
            Modu_AddConsoleMessage(ctx,
                                   "StandaloneMovementController_C: add Rigidbody for physics movement.",
                                   MODU_CONSOLE_WARNING);
            state->warnedMissingRigidbody = 1;
        }
        pos = Modu_GetPosition(ctx);
        pos.x += velocity.x * dt;
        pos.y += velocity.y * dt;
        pos.z += velocity.z * dt;
        if (pos.y < capsuleHalf) {
            pos.y = capsuleHalf;
            state->verticalVelocity = 0.0f;
        }
        Modu_SetPosition(ctx, pos);
    }
}

