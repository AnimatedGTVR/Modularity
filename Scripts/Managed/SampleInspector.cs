using System;

namespace ModuCPP {
    public static class SampleInspector {
        private static bool autoRotate = false;
        private static Vec3 spinSpeed = new Vec3(0f, 45f, 0f);
        private static Vec3 offset = new Vec3(0f, 1f, 0f);
        private static string targetName = "MyTarget"; // Stored for parity; object lookup API not wired yet.

        private static void LoadSettings(Context context) {
            autoRotate = context.GetSettingBool("autoRotate", autoRotate);
            spinSpeed = new Vec3(
                context.GetSettingFloat("spinSpeedX", spinSpeed.X),
                context.GetSettingFloat("spinSpeedY", spinSpeed.Y),
                context.GetSettingFloat("spinSpeedZ", spinSpeed.Z)
            );
            offset = new Vec3(
                context.GetSettingFloat("offsetX", offset.X),
                context.GetSettingFloat("offsetY", offset.Y),
                context.GetSettingFloat("offsetZ", offset.Z)
            );
            targetName = context.GetSettingString("targetName", targetName);
        }

        private static void SaveSettings(Context context) {
            context.SetSettingBool("autoRotate", autoRotate);
            context.SetSettingFloat("spinSpeedX", spinSpeed.X);
            context.SetSettingFloat("spinSpeedY", spinSpeed.Y);
            context.SetSettingFloat("spinSpeedZ", spinSpeed.Z);
            context.SetSettingFloat("offsetX", offset.X);
            context.SetSettingFloat("offsetY", offset.Y);
            context.SetSettingFloat("offsetZ", offset.Z);
            context.SetSettingString("targetName", targetName);
        }

        private static void ApplyAutoRotate(Context context, float deltaTime) {
            if (!autoRotate) return;
            context.Rotation = context.Rotation + (spinSpeed * deltaTime);
        }

        public static void Script_Begin(IntPtr ctx, float deltaTime) {
            var context = new Context(ctx);
            LoadSettings(context);
            SaveSettings(context);
            context.EnsureRigidbody(useGravity: true, kinematic: false);
            context.AddConsoleMessage("Managed script begin (C#)", ConsoleMessageType.Info);
        }

        public static void Script_OnInspector(IntPtr ctx) {
            var context = new Context(ctx);
            LoadSettings(context);
            SaveSettings(context);
            context.AddConsoleMessage("Managed inspector hook (no UI yet)", ConsoleMessageType.Info);
        }

        public static void Script_Spec(IntPtr ctx, float deltaTime) {
            ApplyAutoRotate(new Context(ctx), deltaTime);
        }

        public static void Script_TestEditor(IntPtr ctx, float deltaTime) {
            ApplyAutoRotate(new Context(ctx), deltaTime);
        }

        public static void Script_TickUpdate(IntPtr ctx, float deltaTime) {
            ApplyAutoRotate(new Context(ctx), deltaTime);
        }
    }
}
