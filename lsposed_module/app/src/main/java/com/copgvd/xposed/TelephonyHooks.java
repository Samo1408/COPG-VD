package com.copgvd.xposed;

import android.util.Log;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class TelephonyHooks {

    private static final String TAG = "COPGVD-Telephony";

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        if (!SpoofConfig.get().isSpoofEnabled()) return;

        Class<?> clazz = XposedHelpers.findClassIfExists(
                "android.telephony.TelephonyManager", lpparam.classLoader);
        if (clazz == null) { Log.w(TAG, "TelephonyManager class not found"); return; }

        hook(clazz, "getNetworkCountryIso", "SIM_ISO");
        hook(clazz, "getSimCountryIso", "SIM_ISO");
        hook(clazz, "getNetworkOperatorName", "SIM_CARRIER");
        hook(clazz, "getSimOperatorName", "SIM_CARRIER");
        hook(clazz, "getNetworkOperator", "SIM_MCCMNC");
        hook(clazz, "getSimOperator", "SIM_MCCMNC");

        Log.i(TAG, "TelephonyManager hooks installed");
    }

    private static void hook(Class<?> clazz, String method, String key) {
        try {
            XposedHelpers.findAndHookMethod(clazz, method, new XC_MethodHook() {
                @Override
                protected void beforeHookedMethod(MethodHookParam param) {
                    String v = SpoofConfig.get().getSimGpsField(key, "");
                    if (!v.isEmpty()) param.setResult(v);
                }
            });
        } catch (Throwable t) { Log.e(TAG, "Hook failed: " + method, t); }
    }
}
