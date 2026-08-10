package com.copgvd.xposed;

import java.util.UUID;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class MediaDrmHooks {

    private static final UUID WIDEVINE = UUID.fromString("edef8ba9-79d6-4ace-a3c8-27dcd51d21ed");

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        if (!cfg.isSpoofEnabled()) return;

        final String drmId = cfg.getMediaDrmId();
        final String level = cfg.getMediaDrmLevel();
        if (drmId.isEmpty() && level.isEmpty()) return;

        Class<?> cls = XposedHelpers.findClassIfExists(
                "android.media.MediaDrm", lpparam.classLoader);
        if (cls == null) return;

        // Hook getPropertyByteArray for deviceUniqueId
        if (!drmId.isEmpty()) {
            try {
                final byte[] idBytes = hexToBytes(drmId);
                XposedHelpers.findAndHookMethod(cls, "getPropertyByteArray",
                    UUID.class, String.class, new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) {
                            if ("deviceUniqueId".equals(p.args[1]) && WIDEVINE.equals(p.args[0])) {
                                p.setResult(idBytes);
                            }
                        }
                    });
                XposedBridge.log("COPGVD-DRM: ID hooked (" + drmId.length() + " hex chars)");
            } catch (Throwable t) {
                XposedBridge.log("COPGVD-DRM: getPropertyByteArray failed: " + t.getMessage());
            }
        }

        // Hook getPropertyString for securityLevel
        if (!level.isEmpty()) {
            try {
                XposedHelpers.findAndHookMethod(cls, "getPropertyString",
                    UUID.class, String.class, new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) {
                            if ("securityLevel".equals(p.args[1]) && WIDEVINE.equals(p.args[0])) {
                                p.setResult(level);
                            }
                        }
                    });
                XposedBridge.log("COPGVD-DRM: securityLevel -> " + level);
            } catch (Throwable t) {
                XposedBridge.log("COPGVD-DRM: getPropertyString failed: " + t.getMessage());
            }
        }
    }

    private static byte[] hexToBytes(String hex) {
        int len = hex.length();
        byte[] data = new byte[len / 2];
        for (int i = 0; i < len; i += 2)
            data[i / 2] = (byte) ((Character.digit(hex.charAt(i), 16) << 4)
                    + Character.digit(hex.charAt(i + 1), 16));
        return data;
    }
}
