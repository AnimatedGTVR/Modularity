#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ModuScriptContext ModuScriptContext;

typedef struct ModuVec3 {
    float x;
    float y;
    float z;
} ModuVec3;

enum ModuConsoleMessageType {
    MODU_CONSOLE_INFO = 0,
    MODU_CONSOLE_WARNING = 1,
    MODU_CONSOLE_ERROR = 2,
    MODU_CONSOLE_SUCCESS = 3
};

int Modu_GetObjectId(ModuScriptContext* ctx);
int Modu_IsObjectEnabled(ModuScriptContext* ctx);
void Modu_SetObjectEnabled(ModuScriptContext* ctx, int enabled);

ModuVec3 Modu_GetPosition(ModuScriptContext* ctx);
ModuVec3 Modu_GetRotation(ModuScriptContext* ctx);
ModuVec3 Modu_GetScale(ModuScriptContext* ctx);
void Modu_SetPosition(ModuScriptContext* ctx, ModuVec3 value);
void Modu_SetRotation(ModuScriptContext* ctx, ModuVec3 value);
void Modu_SetScale(ModuScriptContext* ctx, ModuVec3 value);

int Modu_SetRigidbodyVelocity(ModuScriptContext* ctx, ModuVec3 velocity);
int Modu_AddRigidbodyForce(ModuScriptContext* ctx, ModuVec3 force);

void Modu_AddConsoleMessage(ModuScriptContext* ctx, const char* message, int type);

float Modu_GetSettingFloat(ModuScriptContext* ctx, const char* key, float fallback);
void Modu_SetSettingFloat(ModuScriptContext* ctx, const char* key, float value);
int Modu_GetSettingBool(ModuScriptContext* ctx, const char* key, int fallback);
void Modu_SetSettingBool(ModuScriptContext* ctx, const char* key, int value);
void Modu_SetSettingString(ModuScriptContext* ctx, const char* key, const char* value);
int Modu_GetSettingString(ModuScriptContext* ctx, const char* key, const char* fallback,
                          char* outBuffer, int outBufferSize);

#ifdef __cplusplus
}
#endif
