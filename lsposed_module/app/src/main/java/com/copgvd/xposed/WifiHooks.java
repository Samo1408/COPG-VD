package com.copgvd.xposed;

import android.util.Log;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class WifiHooks {

    private static final String TAG = "COPGVD-WiFi";

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        String ssid = SpoofConfig.get().getWifiSsid();
        if (ssid.isEmpty()) return;

        try {
            Class<?> cls = XposedHelpers.findClassIfExists(
                    "android.net.wifi.WifiInfo", lpparam.classLoader);
            if (cls == null) { Log.w(TAG, "WifiInfo class not found"); return; }

            XposedHelpers.findAndHookMethod(cls, "getSSID", new XC_MethodHook() {
                @Override
                protected void beforeHookedMethod(MethodHookParam param) {
                    String s = SpoofConfig.get().getWifiSsid();
                    if (!s.isEmpty()) param.setResult(s);
                }
            });

            Log.i(TAG, "WifiInfo.getSSID() hooked: " + ssid);
        } catch (Throwable t) { Log.e(TAG, "WiFi hook failed", t); }
    }
}
