package com.copgvd.xposed;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class WifiHooks {

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        String ssid = SpoofConfig.get().getWifiSsid();
        if (ssid.isEmpty()) return;

        try {
            Class<?> cls = XposedHelpers.findClassIfExists(
                    "android.net.wifi.WifiInfo", lpparam.classLoader);
            if (cls == null) return;

            XposedHelpers.findAndHookMethod(cls, "getSSID", new XC_MethodHook() {
                @Override
                protected void beforeHookedMethod(MethodHookParam p) {
                    p.setResult(ssid);
                }
            });

            XposedBridge.log("COPGVD-WiFi: SSID -> " + ssid);
        } catch (Throwable t) {
            XposedBridge.log("COPGVD-WiFi: error " + t.getMessage());
        }
    }
}
