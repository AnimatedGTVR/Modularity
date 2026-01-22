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

        private static string FromUtf8(byte* ptr) {
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
}
