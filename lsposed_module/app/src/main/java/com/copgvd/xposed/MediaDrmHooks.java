package com.copgvd.xposed;

import android.util.Log;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class MediaDrmHooks {

    private static final String TAG = "COPGVD-MediaDrm";
    private static final UUID WIDEVINE_UUID = UUID.fromString("edef8ba9-79d6-4ace-a3c8-27dcd51d21ed");
    private static final Map<String, byte[]> spoofedIds = new HashMap<>();

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        if (!cfg.isSpoofEnabled()) return;
        String drmId = cfg.getMediaDrmId();
        if (drmId.isEmpty()) return;

        spoofedIds.put(WIDEVINE_UUID.toString(), hexToBytes(drmId));

        Class<?> clazz = XposedHelpers.findClassIfExists(
                "android.media.MediaDrm", lpparam.classLoader);
        if (clazz == null) { Log.w(TAG, "MediaDrm class not found"); return; }

        // Hook getPropertyByteArray for deviceUniqueId
        try {
            XposedHelpers.findAndHookMethod(clazz, "getPropertyByteArray",
                    UUID.class, String.class, new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam param) {
                            String prop = (String) param.args[1];
                            String uuid = param.args[0].toString();
                            if ("deviceUniqueId".equals(prop) && spoofedIds.containsKey(uuid)) {
                                param.setResult(spoofedIds.get(uuid));
                            }
                        }
                    });
            Log.i(TAG, "MediaDrm.getPropertyByteArray hooked");
        } catch (Throwable t) { Log.e(TAG, "getPropertyByteArray hook failed", t); }

        // Hook getPropertyString for securityLevel
        try {
            XposedHelpers.findAndHookMethod(clazz, "getPropertyString",
                    UUID.class, String.class, new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam param) {
                            if ("securityLevel".equals(param.args[1])
                                    && WIDEVINE_UUID.equals(param.args[0])) {
                                String level = SpoofConfig.get().getMediaDrmLevel();
                                if (!level.isEmpty()) param.setResult(level);
                            }
                        }
                    });
            Log.i(TAG, "MediaDrm.getPropertyString hooked");
        } catch (Throwable t) { Log.e(TAG, "getPropertyString hook failed", t); }
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
