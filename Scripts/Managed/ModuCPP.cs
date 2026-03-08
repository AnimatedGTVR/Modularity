using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ModuCPP {
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ScriptTickDelegate(IntPtr ctx, float deltaTime);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ScriptInspectorDelegate(IntPtr ctx);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void SetNativeApiDelegate(IntPtr apiPtr);

    [StructLayout(LayoutKind.Sequential)]
    public struct Vec3 {
        public float X;
        public float Y;
        public float Z;

        public Vec3(float x, float y, float z) {
            X = x;
            Y = y;
            Z = z;
        }

        public static Vec3 operator +(Vec3 a, Vec3 b) => new Vec3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vec3 operator -(Vec3 a, Vec3 b) => new Vec3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vec3 operator *(Vec3 a, float s) => new Vec3(a.X * s, a.Y * s, a.Z * s);
    }

    public enum ConsoleMessageType {
        Info = 0,
        Warning = 1,
        Error = 2,
        Success = 3
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeApi {
        public uint Version;
        public IntPtr GetObjectId;
        public IntPtr GetPosition;
        public IntPtr SetPosition;
        public IntPtr GetRotation;
        public IntPtr SetRotation;
        public IntPtr GetScale;
        public IntPtr SetScale;
        public IntPtr HasRigidbody;
        public IntPtr EnsureRigidbody;
        public IntPtr SetRigidbodyVelocity;
        public IntPtr GetRigidbodyVelocity;
        public IntPtr AddRigidbodyForce;
        public IntPtr AddRigidbodyImpulse;
        public IntPtr GetSettingFloat;
        public IntPtr GetSettingBool;
        public IntPtr GetSettingString;
        public IntPtr SetSettingFloat;
        public IntPtr SetSettingBool;
        public IntPtr SetSettingString;
        public IntPtr AddConsoleMessage;
        public IntPtr FindObjectIdByName;
        public IntPtr GetObjectName;
        public IntPtr GetSelectedObjectId;
        public IntPtr ImGuiText;
        public IntPtr ImGuiSeparator;
        public IntPtr ImGuiButton;
        public IntPtr ImGuiCheckbox;
        public IntPtr ImGuiDragFloat;
        public IntPtr ImGuiDragFloat3;
        public IntPtr ImGuiInputText;
        public IntPtr ImGuiAcceptSceneObjectDrop;
        // Version 4+ additions appended to preserve ABI layout for older managed assemblies.
        public IntPtr GetSceneObjectCount;
        public IntPtr GetSceneObjectIdAt;
        public IntPtr ImGuiBeginCombo;
        public IntPtr ImGuiEndCombo;
        public IntPtr ImGuiSelectable;
        // Version 5+ additions.
        public IntPtr HasAnimation;
        public IntPtr PlayAnimation;
        public IntPtr StopAnimation;
        public IntPtr PauseAnimation;
        public IntPtr ReverseAnimation;
        public IntPtr SetAnimationTime;
        public IntPtr GetAnimationTime;
        public IntPtr IsAnimationPlaying;
        public IntPtr SetAnimationLoop;
        public IntPtr SetAnimationPlaySpeed;
        public IntPtr SetAnimationPlayOnAwake;
    }

    internal unsafe static class Native {
        public static NativeApi Api;
        public static GetObjectIdFn GetObjectId;
        public static GetPositionFn GetPosition;
        public static SetPositionFn SetPosition;
        public static GetRotationFn GetRotation;
        public static SetRotationFn SetRotation;
        public static GetScaleFn GetScale;
        public static SetScaleFn SetScale;
        public static HasRigidbodyFn HasRigidbody;
        public static EnsureRigidbodyFn EnsureRigidbody;
        public static SetRigidbodyVelocityFn SetRigidbodyVelocity;
        public static GetRigidbodyVelocityFn GetRigidbodyVelocity;
        public static AddRigidbodyForceFn AddRigidbodyForce;
        public static AddRigidbodyImpulseFn AddRigidbodyImpulse;
        public static GetSettingFloatFn GetSettingFloat;
        public static GetSettingBoolFn GetSettingBool;
        public static GetSettingStringFn GetSettingString;
        public static SetSettingFloatFn SetSettingFloat;
        public static SetSettingBoolFn SetSettingBool;
        public static SetSettingStringFn SetSettingString;
        public static AddConsoleMessageFn AddConsoleMessage;
        public static FindObjectIdByNameFn FindObjectIdByName;
        public static GetObjectNameFn GetObjectName;
        public static GetSelectedObjectIdFn GetSelectedObjectId;
        public static GetSceneObjectCountFn GetSceneObjectCount;
        public static GetSceneObjectIdAtFn GetSceneObjectIdAt;
        public static ImGuiTextFn ImGuiText;
        public static ImGuiSeparatorFn ImGuiSeparator;
        public static ImGuiButtonFn ImGuiButton;
        public static ImGuiCheckboxFn ImGuiCheckbox;
        public static ImGuiDragFloatFn ImGuiDragFloat;
        public static ImGuiDragFloat3Fn ImGuiDragFloat3;
        public static ImGuiInputTextFn ImGuiInputText;
        public static ImGuiBeginComboFn ImGuiBeginCombo;
        public static ImGuiEndComboFn ImGuiEndCombo;
        public static ImGuiSelectableFn ImGuiSelectable;
        public static ImGuiAcceptSceneObjectDropFn ImGuiAcceptSceneObjectDrop;
        public static HasAnimationFn HasAnimation;
        public static PlayAnimationFn PlayAnimation;
        public static StopAnimationFn StopAnimation;
        public static PauseAnimationFn PauseAnimation;
        public static ReverseAnimationFn ReverseAnimation;
        public static SetAnimationTimeFn SetAnimationTime;
        public static GetAnimationTimeFn GetAnimationTime;
        public static IsAnimationPlayingFn IsAnimationPlaying;
        public static SetAnimationLoopFn SetAnimationLoop;
        public static SetAnimationPlaySpeedFn SetAnimationPlaySpeed;
        public static SetAnimationPlayOnAwakeFn SetAnimationPlayOnAwake;

        public static void BindDelegates() {
            GetObjectId = Marshal.GetDelegateForFunctionPointer<GetObjectIdFn>(Api.GetObjectId);
            GetPosition = Marshal.GetDelegateForFunctionPointer<GetPositionFn>(Api.GetPosition);
            SetPosition = Marshal.GetDelegateForFunctionPointer<SetPositionFn>(Api.SetPosition);
            GetRotation = Marshal.GetDelegateForFunctionPointer<GetRotationFn>(Api.GetRotation);
            SetRotation = Marshal.GetDelegateForFunctionPointer<SetRotationFn>(Api.SetRotation);
            GetScale = Marshal.GetDelegateForFunctionPointer<GetScaleFn>(Api.GetScale);
            SetScale = Marshal.GetDelegateForFunctionPointer<SetScaleFn>(Api.SetScale);
            HasRigidbody = Marshal.GetDelegateForFunctionPointer<HasRigidbodyFn>(Api.HasRigidbody);
            EnsureRigidbody = Marshal.GetDelegateForFunctionPointer<EnsureRigidbodyFn>(Api.EnsureRigidbody);
            SetRigidbodyVelocity = Marshal.GetDelegateForFunctionPointer<SetRigidbodyVelocityFn>(Api.SetRigidbodyVelocity);
            GetRigidbodyVelocity = Marshal.GetDelegateForFunctionPointer<GetRigidbodyVelocityFn>(Api.GetRigidbodyVelocity);
            AddRigidbodyForce = Marshal.GetDelegateForFunctionPointer<AddRigidbodyForceFn>(Api.AddRigidbodyForce);
            AddRigidbodyImpulse = Marshal.GetDelegateForFunctionPointer<AddRigidbodyImpulseFn>(Api.AddRigidbodyImpulse);
            GetSettingFloat = Marshal.GetDelegateForFunctionPointer<GetSettingFloatFn>(Api.GetSettingFloat);
            GetSettingBool = Marshal.GetDelegateForFunctionPointer<GetSettingBoolFn>(Api.GetSettingBool);
            GetSettingString = Marshal.GetDelegateForFunctionPointer<GetSettingStringFn>(Api.GetSettingString);
            SetSettingFloat = Marshal.GetDelegateForFunctionPointer<SetSettingFloatFn>(Api.SetSettingFloat);
            SetSettingBool = Marshal.GetDelegateForFunctionPointer<SetSettingBoolFn>(Api.SetSettingBool);
            SetSettingString = Marshal.GetDelegateForFunctionPointer<SetSettingStringFn>(Api.SetSettingString);
            AddConsoleMessage = Marshal.GetDelegateForFunctionPointer<AddConsoleMessageFn>(Api.AddConsoleMessage);
            FindObjectIdByName = Marshal.GetDelegateForFunctionPointer<FindObjectIdByNameFn>(Api.FindObjectIdByName);
            GetObjectName = Marshal.GetDelegateForFunctionPointer<GetObjectNameFn>(Api.GetObjectName);
            GetSelectedObjectId = Marshal.GetDelegateForFunctionPointer<GetSelectedObjectIdFn>(Api.GetSelectedObjectId);
            ImGuiText = Marshal.GetDelegateForFunctionPointer<ImGuiTextFn>(Api.ImGuiText);
            ImGuiSeparator = Marshal.GetDelegateForFunctionPointer<ImGuiSeparatorFn>(Api.ImGuiSeparator);
            ImGuiButton = Marshal.GetDelegateForFunctionPointer<ImGuiButtonFn>(Api.ImGuiButton);
            ImGuiCheckbox = Marshal.GetDelegateForFunctionPointer<ImGuiCheckboxFn>(Api.ImGuiCheckbox);
            ImGuiDragFloat = Marshal.GetDelegateForFunctionPointer<ImGuiDragFloatFn>(Api.ImGuiDragFloat);
            ImGuiDragFloat3 = Marshal.GetDelegateForFunctionPointer<ImGuiDragFloat3Fn>(Api.ImGuiDragFloat3);
            ImGuiInputText = Marshal.GetDelegateForFunctionPointer<ImGuiInputTextFn>(Api.ImGuiInputText);
            ImGuiAcceptSceneObjectDrop = Marshal.GetDelegateForFunctionPointer<ImGuiAcceptSceneObjectDropFn>(Api.ImGuiAcceptSceneObjectDrop);

            // Optional API extensions (v4+). Keep safe fallbacks for older native layouts.
            if (Api.Version >= 4 && Api.GetSceneObjectCount != IntPtr.Zero) {
                GetSceneObjectCount = Marshal.GetDelegateForFunctionPointer<GetSceneObjectCountFn>(Api.GetSceneObjectCount);
            } else {
                GetSceneObjectCount = _ => 0;
            }
            if (Api.Version >= 4 && Api.GetSceneObjectIdAt != IntPtr.Zero) {
                GetSceneObjectIdAt = Marshal.GetDelegateForFunctionPointer<GetSceneObjectIdAtFn>(Api.GetSceneObjectIdAt);
            } else {
                GetSceneObjectIdAt = (_, _) => -1;
            }
            if (Api.Version >= 4 && Api.ImGuiBeginCombo != IntPtr.Zero) {
                ImGuiBeginCombo = Marshal.GetDelegateForFunctionPointer<ImGuiBeginComboFn>(Api.ImGuiBeginCombo);
            } else {
                ImGuiBeginCombo = (_, _) => 0;
            }
            if (Api.Version >= 4 && Api.ImGuiEndCombo != IntPtr.Zero) {
                ImGuiEndCombo = Marshal.GetDelegateForFunctionPointer<ImGuiEndComboFn>(Api.ImGuiEndCombo);
            } else {
                ImGuiEndCombo = () => { };
            }
            if (Api.Version >= 4 && Api.ImGuiSelectable != IntPtr.Zero) {
                ImGuiSelectable = Marshal.GetDelegateForFunctionPointer<ImGuiSelectableFn>(Api.ImGuiSelectable);
            } else {
                ImGuiSelectable = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.HasAnimation != IntPtr.Zero) {
                HasAnimation = Marshal.GetDelegateForFunctionPointer<HasAnimationFn>(Api.HasAnimation);
            } else {
                HasAnimation = _ => 0;
            }
            if (Api.Version >= 5 && Api.PlayAnimation != IntPtr.Zero) {
                PlayAnimation = Marshal.GetDelegateForFunctionPointer<PlayAnimationFn>(Api.PlayAnimation);
            } else {
                PlayAnimation = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.StopAnimation != IntPtr.Zero) {
                StopAnimation = Marshal.GetDelegateForFunctionPointer<StopAnimationFn>(Api.StopAnimation);
            } else {
                StopAnimation = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.PauseAnimation != IntPtr.Zero) {
                PauseAnimation = Marshal.GetDelegateForFunctionPointer<PauseAnimationFn>(Api.PauseAnimation);
            } else {
                PauseAnimation = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.ReverseAnimation != IntPtr.Zero) {
                ReverseAnimation = Marshal.GetDelegateForFunctionPointer<ReverseAnimationFn>(Api.ReverseAnimation);
            } else {
                ReverseAnimation = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.SetAnimationTime != IntPtr.Zero) {
                SetAnimationTime = Marshal.GetDelegateForFunctionPointer<SetAnimationTimeFn>(Api.SetAnimationTime);
            } else {
                SetAnimationTime = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.GetAnimationTime != IntPtr.Zero) {
                GetAnimationTime = Marshal.GetDelegateForFunctionPointer<GetAnimationTimeFn>(Api.GetAnimationTime);
            } else {
                GetAnimationTime = _ => 0f;
            }
            if (Api.Version >= 5 && Api.IsAnimationPlaying != IntPtr.Zero) {
                IsAnimationPlaying = Marshal.GetDelegateForFunctionPointer<IsAnimationPlayingFn>(Api.IsAnimationPlaying);
            } else {
                IsAnimationPlaying = _ => 0;
            }
            if (Api.Version >= 5 && Api.SetAnimationLoop != IntPtr.Zero) {
                SetAnimationLoop = Marshal.GetDelegateForFunctionPointer<SetAnimationLoopFn>(Api.SetAnimationLoop);
            } else {
                SetAnimationLoop = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.SetAnimationPlaySpeed != IntPtr.Zero) {
                SetAnimationPlaySpeed = Marshal.GetDelegateForFunctionPointer<SetAnimationPlaySpeedFn>(Api.SetAnimationPlaySpeed);
            } else {
                SetAnimationPlaySpeed = (_, _) => 0;
            }
            if (Api.Version >= 5 && Api.SetAnimationPlayOnAwake != IntPtr.Zero) {
                SetAnimationPlayOnAwake = Marshal.GetDelegateForFunctionPointer<SetAnimationPlayOnAwakeFn>(Api.SetAnimationPlayOnAwake);
            } else {
                SetAnimationPlayOnAwake = (_, _) => 0;
            }
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int GetObjectIdFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void GetPositionFn(IntPtr ctx, float* x, float* y, float* z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void SetPositionFn(IntPtr ctx, float x, float y, float z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void GetRotationFn(IntPtr ctx, float* x, float* y, float* z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void SetRotationFn(IntPtr ctx, float x, float y, float z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void GetScaleFn(IntPtr ctx, float* x, float* y, float* z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void SetScaleFn(IntPtr ctx, float x, float y, float z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int HasRigidbodyFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int EnsureRigidbodyFn(IntPtr ctx, int useGravity, int kinematic);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int SetRigidbodyVelocityFn(IntPtr ctx, float x, float y, float z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int GetRigidbodyVelocityFn(IntPtr ctx, float* x, float* y, float* z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int AddRigidbodyForceFn(IntPtr ctx, float x, float y, float z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int AddRigidbodyImpulseFn(IntPtr ctx, float x, float y, float z);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate float GetSettingFloatFn(IntPtr ctx, byte* key, float fallback);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int GetSettingBoolFn(IntPtr ctx, byte* key, int fallback);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void GetSettingStringFn(IntPtr ctx, byte* key, byte* fallback, byte* outBuffer, int outBufferSize);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void SetSettingFloatFn(IntPtr ctx, byte* key, float value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void SetSettingBoolFn(IntPtr ctx, byte* key, int value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void SetSettingStringFn(IntPtr ctx, byte* key, byte* value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void AddConsoleMessageFn(IntPtr ctx, byte* message, int type);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int FindObjectIdByNameFn(IntPtr ctx, byte* name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void GetObjectNameFn(IntPtr ctx, int id, byte* outBuffer, int outBufferSize);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int GetSelectedObjectIdFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int GetSceneObjectCountFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int GetSceneObjectIdAtFn(IntPtr ctx, int index);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void ImGuiTextFn(byte* text);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void ImGuiSeparatorFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiButtonFn(byte* label);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiCheckboxFn(byte* label, int* value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiDragFloatFn(byte* label, float* value, float speed, float minValue, float maxValue);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiDragFloat3Fn(byte* label, float* values, float speed, float minValue, float maxValue);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiInputTextFn(byte* label, byte* buffer, int bufferSize);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiBeginComboFn(byte* label, byte* previewValue);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void ImGuiEndComboFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiSelectableFn(byte* label, int selected);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ImGuiAcceptSceneObjectDropFn(int* outId);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int HasAnimationFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int PlayAnimationFn(IntPtr ctx, int restart);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int StopAnimationFn(IntPtr ctx, int resetTime);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int PauseAnimationFn(IntPtr ctx, int pause);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ReverseAnimationFn(IntPtr ctx, int restartIfStopped);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int SetAnimationTimeFn(IntPtr ctx, float timeSeconds);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate float GetAnimationTimeFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int IsAnimationPlayingFn(IntPtr ctx);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int SetAnimationLoopFn(IntPtr ctx, int loop);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int SetAnimationPlaySpeedFn(IntPtr ctx, float speed);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int SetAnimationPlayOnAwakeFn(IntPtr ctx, int playOnAwake);
    }

    public struct ModuObject {
        public int Id;
        public string Name;

        public bool IsValid => Id >= 0;

        public ModuObject(int id, string name) {
            Id = id;
            Name = name ?? string.Empty;
        }

        public override string ToString() {
            return IsValid ? $"{Name} ({Id})" : "<None>";
        }
    }

    public static unsafe class Host {
        public static void SetNativeApi(IntPtr apiPtr) {
            Native.Api = Marshal.PtrToStructure<NativeApi>(apiPtr);
            Native.BindDelegates();
        }
    }

    public readonly unsafe struct Context {
        private readonly IntPtr handle;

        public Context(IntPtr ctx) {
            handle = ctx;
        }

        public int ObjectId => Native.GetObjectId(handle);
        public int SelectedObjectId => Native.GetSelectedObjectId(handle);
        public int SceneObjectCount => Native.GetSceneObjectCount(handle);

        public ModuObject FindObjectByName(string name) {
            if (string.IsNullOrEmpty(name)) return new ModuObject(-1, string.Empty);
            byte[] nameBytes = Encoding.UTF8.GetBytes(name + "\0");
            fixed (byte* namePtr = nameBytes) {
                int id = Native.FindObjectIdByName(handle, namePtr);
                return new ModuObject(id, GetObjectName(id));
            }
        }

        public ModuObject FindObjectById(int id) {
            if (id < 0) return new ModuObject(-1, string.Empty);
            return new ModuObject(id, GetObjectName(id));
        }

        public ModuObject GetObjectByIndex(int index) {
            if (index < 0) return new ModuObject(-1, string.Empty);
            int id = Native.GetSceneObjectIdAt(handle, index);
            if (id < 0) return new ModuObject(-1, string.Empty);
            return new ModuObject(id, GetObjectName(id));
        }

        public string GetObjectName(int id) {
            if (id < 0) return string.Empty;
            const int bufferSize = 256;
            byte* buffer = stackalloc byte[bufferSize];
            Native.GetObjectName(handle, id, buffer, bufferSize);
            return FromUtf8(buffer);
        }

        public Vec3 Position {
            get {
                float x = 0f, y = 0f, z = 0f;
                Native.GetPosition(handle, &x, &y, &z);
                return new Vec3(x, y, z);
            }
            set {
                Native.SetPosition(handle, value.X, value.Y, value.Z);
            }
        }

        public Vec3 Rotation {
            get {
                float x = 0f, y = 0f, z = 0f;
                Native.GetRotation(handle, &x, &y, &z);
                return new Vec3(x, y, z);
            }
            set {
                Native.SetRotation(handle, value.X, value.Y, value.Z);
            }
        }

        public Vec3 Scale {
            get {
                float x = 0f, y = 0f, z = 0f;
                Native.GetScale(handle, &x, &y, &z);
                return new Vec3(x, y, z);
            }
            set {
                Native.SetScale(handle, value.X, value.Y, value.Z);
            }
        }

        public bool HasRigidbody => Native.HasRigidbody(handle) != 0;

        public bool EnsureRigidbody(bool useGravity = true, bool kinematic = false) {
            return Native.EnsureRigidbody(handle, useGravity ? 1 : 0, kinematic ? 1 : 0) != 0;
        }

        public Vec3 RigidbodyVelocity {
            get {
                float x = 0f, y = 0f, z = 0f;
                if (Native.GetRigidbodyVelocity(handle, &x, &y, &z) == 0) {
                    return new Vec3(0f, 0f, 0f);
                }
                return new Vec3(x, y, z);
            }
            set {
                Native.SetRigidbodyVelocity(handle, value.X, value.Y, value.Z);
            }
        }

        public void AddRigidbodyForce(Vec3 force) {
            Native.AddRigidbodyForce(handle, force.X, force.Y, force.Z);
        }

        public void AddRigidbodyImpulse(Vec3 impulse) {
            Native.AddRigidbodyImpulse(handle, impulse.X, impulse.Y, impulse.Z);
        }

        public bool HasAnimation => Native.HasAnimation(handle) != 0;

        public bool PlayAnimation(bool restart = true) {
            return Native.PlayAnimation(handle, restart ? 1 : 0) != 0;
        }

        public bool StopAnimation(bool resetTime = true) {
            return Native.StopAnimation(handle, resetTime ? 1 : 0) != 0;
        }

        public bool PauseAnimation(bool pause = true) {
            return Native.PauseAnimation(handle, pause ? 1 : 0) != 0;
        }

        public bool ReverseAnimation(bool restartIfStopped = true) {
            return Native.ReverseAnimation(handle, restartIfStopped ? 1 : 0) != 0;
        }

        public bool SetAnimationTime(float timeSeconds) {
            return Native.SetAnimationTime(handle, timeSeconds) != 0;
        }

        public float GetAnimationTime() {
            return Native.GetAnimationTime(handle);
        }

        public bool IsAnimationPlaying() {
            return Native.IsAnimationPlaying(handle) != 0;
        }

        public bool SetAnimationLoop(bool loop) {
            return Native.SetAnimationLoop(handle, loop ? 1 : 0) != 0;
        }

        public bool SetAnimationPlaySpeed(float speed) {
            return Native.SetAnimationPlaySpeed(handle, speed) != 0;
        }

        public bool SetAnimationPlayOnAwake(bool playOnAwake) {
            return Native.SetAnimationPlayOnAwake(handle, playOnAwake ? 1 : 0) != 0;
        }

        public float GetSettingFloat(string key, float fallback = 0f) {
            byte[] keyBytes = Encoding.UTF8.GetBytes((key ?? string.Empty) + "\0");
            fixed (byte* keyPtr = keyBytes) {
                return Native.GetSettingFloat(handle, keyPtr, fallback);
            }
        }

        public bool GetSettingBool(string key, bool fallback = false) {
            byte[] keyBytes = Encoding.UTF8.GetBytes((key ?? string.Empty) + "\0");
            fixed (byte* keyPtr = keyBytes) {
                int value = Native.GetSettingBool(handle, keyPtr, fallback ? 1 : 0);
                return value != 0;
            }
        }

        public string GetSettingString(string key, string fallback = "") {
            const int bufferSize = 256;
            byte[] keyBytes = Encoding.UTF8.GetBytes((key ?? string.Empty) + "\0");
            byte[] fallbackBytes = Encoding.UTF8.GetBytes((fallback ?? string.Empty) + "\0");
            byte* buffer = stackalloc byte[bufferSize];
            fixed (byte* keyPtr = keyBytes)
            fixed (byte* fallbackPtr = fallbackBytes) {
                Native.GetSettingString(handle, keyPtr, fallbackPtr, buffer, bufferSize);
            }
            return FromUtf8(buffer);
        }

        public void SetSettingFloat(string key, float value) {
            byte[] keyBytes = Encoding.UTF8.GetBytes((key ?? string.Empty) + "\0");
            fixed (byte* keyPtr = keyBytes) {
                Native.SetSettingFloat(handle, keyPtr, value);
            }
        }

        public void SetSettingBool(string key, bool value) {
            byte[] keyBytes = Encoding.UTF8.GetBytes((key ?? string.Empty) + "\0");
            fixed (byte* keyPtr = keyBytes) {
                Native.SetSettingBool(handle, keyPtr, value ? 1 : 0);
            }
        }

        public void SetSettingString(string key, string value) {
            byte[] keyBytes = Encoding.UTF8.GetBytes((key ?? string.Empty) + "\0");
            byte[] valueBytes = Encoding.UTF8.GetBytes((value ?? string.Empty) + "\0");
            fixed (byte* keyPtr = keyBytes)
            fixed (byte* valuePtr = valueBytes) {
                Native.SetSettingString(handle, keyPtr, valuePtr);
            }
        }

        public void AddConsoleMessage(string message, ConsoleMessageType type = ConsoleMessageType.Info) {
            byte[] msgBytes = Encoding.UTF8.GetBytes((message ?? string.Empty) + "\0");
            fixed (byte* msgPtr = msgBytes) {
                Native.AddConsoleMessage(handle, msgPtr, (int)type);
            }
        }

        public void AutoSetting(string key, ref bool value, bool save = true) {
            if (!AutoSettings.IsLoaded(handle, key, AutoSettings.ValueType.Bool)) {
                value = GetSettingBool(key, value);
                AutoSettings.MarkLoaded(handle, key, AutoSettings.ValueType.Bool);
            }
            if (save) {
                SetSettingBool(key, value);
            }
        }

        public void AutoSetting(string key, ref float value, bool save = true) {
            if (!AutoSettings.IsLoaded(handle, key, AutoSettings.ValueType.Float)) {
                value = GetSettingFloat(key, value);
                AutoSettings.MarkLoaded(handle, key, AutoSettings.ValueType.Float);
            }
            if (save) {
                SetSettingFloat(key, value);
            }
        }

        public void AutoSetting(string key, ref Vec3 value, bool save = true) {
            if (!AutoSettings.IsLoaded(handle, key, AutoSettings.ValueType.Vec3)) {
                string loaded = GetSettingString(key, "");
                if (!string.IsNullOrEmpty(loaded)) {
                    ParseVec3(loaded, ref value);
                }
                AutoSettings.MarkLoaded(handle, key, AutoSettings.ValueType.Vec3);
            }
            if (save) {
                SetSettingString(key, $"{value.X},{value.Y},{value.Z}");
            }
        }

        public void AutoSetting(string key, ref string value, int bufferSize = 256, bool save = true) {
            if (!AutoSettings.IsLoaded(handle, key, AutoSettings.ValueType.String)) {
                value = GetSettingString(key, value ?? string.Empty);
                AutoSettings.MarkLoaded(handle, key, AutoSettings.ValueType.String);
            }
            if (save) {
                SetSettingString(key, value ?? string.Empty);
            }
        }

        public void AutoSetting(string key, ref ModuObject value, bool save = true) {
            if (!AutoSettings.IsLoaded(handle, key, AutoSettings.ValueType.Object)) {
                string stored = GetSettingString(key, "");
                value = ParseObjectSetting(stored);
                if (!value.IsValid && !string.IsNullOrEmpty(value.Name)) {
                    value = FindObjectByName(value.Name);
                } else if (value.IsValid && string.IsNullOrEmpty(value.Name)) {
                    value.Name = GetObjectName(value.Id);
                }
                AutoSettings.MarkLoaded(handle, key, AutoSettings.ValueType.Object);
            }
            if (save) {
                SetSettingString(key, BuildObjectSetting(value));
            }
        }

        public void AutoSettingsFrom(object instance, bool save = true) {
            if (instance == null) return;
            Inspector.AutoSettingsFrom(this, instance, save);
        }

        private ModuObject ParseObjectSetting(string value) {
            if (string.IsNullOrEmpty(value)) return new ModuObject(-1, string.Empty);
            string[] parts = value.Split('|');
            int id = -1;
            string name = string.Empty;
            if (parts.Length > 0) int.TryParse(parts[0], out id);
            if (parts.Length > 1) name = parts[1];
            return new ModuObject(id, name);
        }

        private string BuildObjectSetting(ModuObject value) {
            if (!value.IsValid && string.IsNullOrEmpty(value.Name)) return string.Empty;
            return $"{value.Id}|{value.Name}";
        }

        private static void ParseVec3(string value, ref Vec3 outVec) {
            if (string.IsNullOrEmpty(value)) return;
            string[] parts = value.Split(',');
            if (parts.Length > 0 && float.TryParse(parts[0], out float x)) outVec.X = x;
            if (parts.Length > 1 && float.TryParse(parts[1], out float y)) outVec.Y = y;
            if (parts.Length > 2 && float.TryParse(parts[2], out float z)) outVec.Z = z;
        }

        internal static string FromUtf8(byte* ptr) {
            if (ptr == null) return string.Empty;
            int length = 0;
            while (ptr[length] != 0) {
                length++;
            }
            if (length == 0) return string.Empty;
            byte[] bytes = new byte[length];
            Marshal.Copy((IntPtr)ptr, bytes, 0, length);
            return Encoding.UTF8.GetString(bytes);
        }
    }

    internal static class AutoSettings {
        internal enum ValueType { Bool, Float, Vec3, String, Object }
        private static readonly System.Collections.Generic.HashSet<string> Loaded =
            new System.Collections.Generic.HashSet<string>();

        internal static bool IsLoaded(IntPtr handle, string key, ValueType type) {
            return Loaded.Contains(MakeKey(handle, key, type));
        }

        internal static void MarkLoaded(IntPtr handle, string key, ValueType type) {
            string id = MakeKey(handle, key, type);
            Loaded.Add(id);
        }

        private static string MakeKey(IntPtr handle, string key, ValueType type) {
            return handle.ToInt64().ToString() + "|" + type.ToString() + "|" + (key ?? string.Empty);
        }
    }

    public static unsafe class ImGui {
        public static void Text(string text) {
            byte[] textBytes = Encoding.UTF8.GetBytes((text ?? string.Empty) + "\0");
            fixed (byte* textPtr = textBytes) {
                Native.ImGuiText(textPtr);
            }
        }

        public static void Separator() {
            Native.ImGuiSeparator();
        }

        public static bool Button(string label) {
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            fixed (byte* labelPtr = labelBytes) {
                return Native.ImGuiButton(labelPtr) != 0;
            }
        }

        public static bool Checkbox(string label, ref bool value) {
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            int v = value ? 1 : 0;
            fixed (byte* labelPtr = labelBytes) {
                int changed = Native.ImGuiCheckbox(labelPtr, &v);
                value = v != 0;
                return changed != 0;
            }
        }

        public static bool DragFloat(string label, ref float value, float speed = 0.1f, float minValue = 0.0f, float maxValue = 0.0f) {
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            fixed (byte* labelPtr = labelBytes)
            fixed (float* valuePtr = &value) {
                return Native.ImGuiDragFloat(labelPtr, valuePtr, speed, minValue, maxValue) != 0;
            }
        }

        public static bool DragFloat3(string label, ref Vec3 value, float speed = 0.1f, float minValue = 0.0f, float maxValue = 0.0f) {
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            fixed (byte* labelPtr = labelBytes)
            fixed (Vec3* valuePtr = &value) {
                return Native.ImGuiDragFloat3(labelPtr, (float*)valuePtr, speed, minValue, maxValue) != 0;
            }
        }

        public static bool InputText(string label, ref string value, int bufferSize = 256) {
            if (bufferSize <= 0) return false;
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            byte[] buffer = new byte[bufferSize];
            string current = value ?? string.Empty;
            int count = Encoding.UTF8.GetBytes(current, 0, current.Length, buffer, 0);
            if (count < bufferSize) {
                buffer[count] = 0;
            } else {
                buffer[bufferSize - 1] = 0;
            }
            fixed (byte* labelPtr = labelBytes)
            fixed (byte* bufferPtr = buffer) {
                int changed = Native.ImGuiInputText(labelPtr, bufferPtr, bufferSize);
                if (changed != 0) {
                    value = Context.FromUtf8(bufferPtr);
                }
                return changed != 0;
            }
        }

        public static bool BeginCombo(string label, string previewValue) {
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            byte[] previewBytes = Encoding.UTF8.GetBytes((previewValue ?? string.Empty) + "\0");
            fixed (byte* labelPtr = labelBytes)
            fixed (byte* previewPtr = previewBytes) {
                return Native.ImGuiBeginCombo(labelPtr, previewPtr) != 0;
            }
        }

        public static void EndCombo() {
            Native.ImGuiEndCombo();
        }

        public static bool Selectable(string label, bool selected = false) {
            byte[] labelBytes = Encoding.UTF8.GetBytes((label ?? string.Empty) + "\0");
            fixed (byte* labelPtr = labelBytes) {
                return Native.ImGuiSelectable(labelPtr, selected ? 1 : 0) != 0;
            }
        }

        public static bool AcceptSceneObjectDrop(out int id) {
            id = -1;
            int result = 0;
            fixed (int* idPtr = &id) {
                result = Native.ImGuiAcceptSceneObjectDrop(idPtr);
            }
            return result != 0;
        }
    }

    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Field)]
    public sealed class HeadTextAttribute : Attribute {
        public string Text { get; }
        public HeadTextAttribute(string text) {
            Text = text;
        }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class LabelAttribute : Attribute {
        public string Text { get; }
        public LabelAttribute(string text) {
            Text = text;
        }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class SettingKeyAttribute : Attribute {
        public string Key { get; }
        public SettingKeyAttribute(string key) {
            Key = key;
        }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class DragSpeedAttribute : Attribute {
        public float Speed { get; }
        public DragSpeedAttribute(float speed) {
            Speed = speed;
        }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class InspectorIgnoreAttribute : Attribute {
    }

    public static class Inspector {
        private static readonly System.Collections.Generic.HashSet<string> ErrorOnce =
            new System.Collections.Generic.HashSet<string>();
        private static readonly System.Collections.Generic.Dictionary<string, string> ObjectPickerFilter =
            new System.Collections.Generic.Dictionary<string, string>();
        private static bool AttributesEnabled = true;
        private static bool AutoSettingsEnabled = true;
        public static void RenderAuto(IntPtr ctx, object instance) {
            if (instance == null) return;
            try {
                var context = new Context(ctx);
                AutoSettingsFrom(context, instance, true);
                AutoInspect(context, instance);
            } catch (Exception ex) {
                var context = new Context(ctx);
                LogOnce(context, "Inspector", instance, ex);
            }
        }

        internal static void AutoSettingsFrom(Context context, object instance, bool save) {
            if (instance == null) return;
            if (!AutoSettingsEnabled) return;
            try {
                var type = instance.GetType();
                var fields = type.GetFields(System.Reflection.BindingFlags.Instance |
                                            System.Reflection.BindingFlags.Public |
                                            System.Reflection.BindingFlags.NonPublic);
                foreach (var field in fields) {
                    // Avoid attribute reflection here; it can trigger missing System.Native on some Mono builds.
                    // Attribute-driven behavior is handled in AutoInspect only.
                    string key = field.Name;
                    var fieldType = field.FieldType;
                    if (fieldType == typeof(bool)) {
                        bool v = (bool)field.GetValue(instance);
                        context.AutoSetting(key, ref v, save);
                        field.SetValue(instance, v);
                    } else if (fieldType == typeof(float)) {
                        float v = (float)field.GetValue(instance);
                        context.AutoSetting(key, ref v, save);
                        field.SetValue(instance, v);
                    } else if (fieldType == typeof(Vec3)) {
                        Vec3 v = (Vec3)field.GetValue(instance);
                        context.AutoSetting(key, ref v, save);
                        field.SetValue(instance, v);
                    } else if (fieldType == typeof(string)) {
                        string v = (string)field.GetValue(instance);
                        context.AutoSetting(key, ref v, 256, save);
                        field.SetValue(instance, v);
                    } else if (fieldType == typeof(ModuObject)) {
                        ModuObject v = (ModuObject)field.GetValue(instance);
                        context.AutoSetting(key, ref v, save);
                        field.SetValue(instance, v);
                    }
                }
            } catch (Exception ex) {
                if (IsSystemNativeMissing(ex)) {
                    AutoSettingsEnabled = false;
                    return;
                }
                LogOnce(context, "AutoSettings", instance, ex);
            }
        }

        public static void AutoInspect(Context context, object instance) {
            if (instance == null) return;
            try {
                var type = instance.GetType();
                if (AttributesEnabled) {
                    var head = TryGetAttribute<HeadTextAttribute>(type);
                    if (head != null && !string.IsNullOrEmpty(head.Text)) {
                        ImGui.Text(head.Text);
                    }
                }

                var fields = type.GetFields(System.Reflection.BindingFlags.Instance |
                                            System.Reflection.BindingFlags.Public |
                                            System.Reflection.BindingFlags.NonPublic);
                foreach (var field in fields) {
                    if (AttributesEnabled && TryGetAttribute<InspectorIgnoreAttribute>(field) != null) continue;
                    var fieldType = field.FieldType;
                    string label = Humanize(field.Name);
                    if (AttributesEnabled) {
                        var labelAttr = TryGetAttribute<LabelAttribute>(field);
                        if (labelAttr != null && !string.IsNullOrEmpty(labelAttr.Text)) {
                            label = labelAttr.Text;
                        }
                        var headAttr = TryGetAttribute<HeadTextAttribute>(field);
                        if (headAttr != null && !string.IsNullOrEmpty(headAttr.Text)) {
                            ImGui.Text(headAttr.Text);
                        }
                    }

                    float speed = 0.1f;
                    if (AttributesEnabled) {
                        var speedAttr = TryGetAttribute<DragSpeedAttribute>(field);
                        if (speedAttr != null) speed = speedAttr.Speed;
                    }

                    if (fieldType == typeof(bool)) {
                        bool v = (bool)field.GetValue(instance);
                        if (ImGui.Checkbox(label, ref v)) {
                            field.SetValue(instance, v);
                        }
                    } else if (fieldType == typeof(float)) {
                        float v = (float)field.GetValue(instance);
                        if (ImGui.DragFloat(label, ref v, speed)) {
                            field.SetValue(instance, v);
                        }
                    } else if (fieldType == typeof(Vec3)) {
                        Vec3 v = (Vec3)field.GetValue(instance);
                        if (ImGui.DragFloat3(label, ref v, speed)) {
                            field.SetValue(instance, v);
                        }
                    } else if (fieldType == typeof(string)) {
                        string v = (string)field.GetValue(instance);
                        if (ImGui.InputText(label, ref v, 256)) {
                            field.SetValue(instance, v);
                        }
                    } else if (fieldType == typeof(ModuObject)) {
                        ModuObject v = (ModuObject)field.GetValue(instance);
                        if (ObjectField(context, label, ref v)) {
                            field.SetValue(instance, v);
                        }
                    }
                }
            } catch (Exception ex) {
                LogOnce(context, "AutoInspect", instance, ex);
            }
        }

        private static void LogOnce(Context context, string stage, object instance, Exception ex) {
            if (instance == null) return;
            string typeName = instance.GetType().FullName ?? instance.GetType().Name;
            string key = stage + ":" + typeName;
            if (ErrorOnce.Contains(key)) return;
            ErrorOnce.Add(key);
            string msg = stage + " failed for " + typeName + ": " + ex.GetType().Name;
            if (!string.IsNullOrEmpty(ex.Message)) {
                msg += " - " + ex.Message;
            }
            if (ex.InnerException != null) {
                msg += " (Inner: " + ex.InnerException.GetType().Name;
                if (!string.IsNullOrEmpty(ex.InnerException.Message)) {
                    msg += " - " + ex.InnerException.Message;
                }
                msg += ")";
            }
            context.AddConsoleMessage(msg, ConsoleMessageType.Error);
        }

        private static bool IsSystemNativeMissing(Exception ex) {
            if (ex is TypeInitializationException && ex.InnerException is DllNotFoundException) {
                string msg = ex.InnerException.Message ?? string.Empty;
                return msg.Contains("System.Native");
            }
            if (ex is DllNotFoundException) {
                string msg = ex.Message ?? string.Empty;
                return msg.Contains("System.Native");
            }
            return false;
        }

        private static T TryGetAttribute<T>(System.Reflection.MemberInfo member) where T : Attribute {
            if (!AttributesEnabled) return null;
            try {
                return (T)Attribute.GetCustomAttribute(member, typeof(T));
            } catch {
                AttributesEnabled = false;
                return null;
            }
        }

        private static bool ObjectField(Context context, string label, ref ModuObject value) {
            bool changed = false;
            if (value.IsValid) {
                string refreshedName = context.GetObjectName(value.Id);
                if (!string.IsNullOrEmpty(refreshedName)) {
                    value.Name = refreshedName;
                }
            }

            ImGui.Text(label);
            string display = value.IsValid ? value.ToString() : "<None>";
            string stableKey = label ?? string.Empty;
            if (!ObjectPickerFilter.TryGetValue(stableKey, out string filter)) {
                filter = string.Empty;
                ObjectPickerFilter[stableKey] = filter;
            }

            bool comboOpen = ImGui.BeginCombo("##ObjectPicker_" + stableKey, display);

            int droppedId;
            if (ImGui.AcceptSceneObjectDrop(out droppedId)) {
                value = context.FindObjectById(droppedId);
                changed = true;
            }

            if (comboOpen) {
                if (ImGui.InputText("Find##ObjectPicker_" + stableKey, ref filter, 128)) {
                    ObjectPickerFilter[stableKey] = filter;
                }

                if (ImGui.Selectable("<None>", !value.IsValid)) {
                    value = new ModuObject(-1, string.Empty);
                    changed = true;
                }

                int objectCount = context.SceneObjectCount;
                for (int i = 0; i < objectCount; ++i) {
                    ModuObject candidate = context.GetObjectByIndex(i);
                    if (!candidate.IsValid) continue;
                    if (!PassesObjectFilter(candidate, filter)) continue;

                    bool selected = value.IsValid && candidate.Id == value.Id;
                    if (ImGui.Selectable(candidate.ToString(), selected)) {
                        value = candidate;
                        changed = true;
                    }
                }

                ImGui.EndCombo();
            }

            if (ImGui.Button("Use Selected##" + stableKey)) {
                int selected = context.SelectedObjectId;
                if (selected >= 0) {
                    value = context.FindObjectById(selected);
                    changed = true;
                }
            }
            if (ImGui.Button("Clear##" + stableKey)) {
                if (value.IsValid || !string.IsNullOrEmpty(value.Name)) {
                    value = new ModuObject(-1, string.Empty);
                    changed = true;
                }
            }
            if (value.IsValid && string.IsNullOrEmpty(value.Name)) {
                value.Name = context.GetObjectName(value.Id);
            }
            return changed;
        }

        private static bool PassesObjectFilter(ModuObject candidate, string filter) {
            if (string.IsNullOrWhiteSpace(filter)) return true;
            string trimmed = filter.Trim();
            if (candidate.Name != null &&
                candidate.Name.IndexOf(trimmed, StringComparison.OrdinalIgnoreCase) >= 0) {
                return true;
            }
            return candidate.Id.ToString().IndexOf(trimmed, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static string Humanize(string name) {
            if (string.IsNullOrEmpty(name)) return string.Empty;
            System.Text.StringBuilder sb = new System.Text.StringBuilder(name.Length + 4);
            sb.Append(char.ToUpper(name[0]));
            for (int i = 1; i < name.Length; ++i) {
                char c = name[i];
                if (char.IsUpper(c) && !char.IsWhiteSpace(name[i - 1])) sb.Append(' ');
                sb.Append(c);
            }
            return sb.ToString();
        }
    }
}
