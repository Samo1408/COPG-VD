package com.copgvd.xposed;

import android.os.Binder;
import android.os.Build;
import android.os.Parcel;
import android.text.TextUtils;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * Hooks writeToParcel() methods in system_server services.
 * When an app queries device info, system_server serializes the response
 * via Parcel. We intercept at this point so ALL apps receive spoofed data,
 * with no hooks in their own process.
 */
public class SystemServiceHooks {

    private static final String TAG = "COPGVD-Sys";

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        hookTelephonyManager(lpparam);
        hookWifiManager(lpparam);
        hookBuild(lpparam);
    }

    // ============================================================
    // TelephonyManager - getService() returns ITelephony
    // ITelephony runs in system_server (com.android.phone process)
    // We hook TelephonyRegistry which notifies apps of network changes
    // ============================================================

    private static void hookTelephonyManager(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        
        try {
            Class<?> tm = XposedHelpers.findClass(
                    "android.telephony.TelephonyManager", lpparam.classLoader);

            // getNetworkCountryIso - hooked directly in system_server
            if (!cfg.getSimIso().isEmpty()) {
                XposedHelpers.findAndHookMethod(tm, "getNetworkCountryIso",
                        new ReturnFix(cfg.getSimIso()));
                XposedBridge.log(TAG + ": Telephony hooks installed");
            }
        } catch (Throwable t) {
            XposedBridge.log(TAG + ": TM hooks: " + t.getMessage());
        }
    }

    // ============================================================
    // WifiService - runs in system_server
    // Hooks on the service side so WifiInfo.getSSID() returns spoofed
    // ============================================================

    private static void hookWifiManager(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        String ssid = cfg.getWifiSsid();
        if (ssid.isEmpty()) return;

        try {
            Class<?> wifiInfo = XposedHelpers.findClass(
                    "android.net.wifi.WifiInfo", lpparam.classLoader);
            XposedHelpers.findAndHookMethod(wifiInfo, "getSSID",
                    new ReturnFix(ssid));
            XposedBridge.log(TAG + ": WifiInfo hooks installed: " + ssid);
        } catch (Throwable t) {
            XposedBridge.log(TAG + ": WiFi hooks: " + t.getMessage());
        }
    }

    // ============================================================
    // Build - static fields set in system_server
    // When apps read Build.MODEL etc, they get what we set here
    // ============================================================

    private static void hookBuild(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        boolean any = false;

        String[][] fields = {
            {"MODEL","MODEL"},{"MANUFACTURER","MANUFACTURER"},{"BRAND","BRAND"},
            {"PRODUCT","PRODUCT"},{"DEVICE","DEVICE"},{"BOARD","BOARD"},
            {"HARDWARE","HARDWARE"},{"FINGERPRINT","FINGERPRINT"},
            {"ID","ID"},{"DISPLAY","DISPLAY"},{"SKU","SKU"},
        };

        for (String[] f : fields) {
            String v = cfg.getDeviceField(f[0], "");
            if (!v.isEmpty() && BuildHooks.setStaticField(Build.class, f[1], v)) any = true;
        }

        BuildHooks.setStaticField(Build.VERSION.class, "RELEASE",
                cfg.getDeviceField("ANDROID_VER", ""));
        BuildHooks.setStaticField(Build.VERSION.class, "INCREMENTAL",
                cfg.getDeviceField("INCREMENTAL", ""));

        if (any) XposedBridge.log(TAG + ": Build hooks installed");
    }

    // Simple XC_MethodHook that always returns a fixed value
    static class ReturnFix extends XC_MethodHook {
        private final Object value;
        ReturnFix(Object v) { this.value = v; }
        @Override
        protected void beforeHookedMethod(MethodHookParam p) throws Throwable {
            p.setResult(value);
        }
    }
}
