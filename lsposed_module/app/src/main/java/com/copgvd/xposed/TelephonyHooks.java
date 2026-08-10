package com.copgvd.xposed;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class TelephonyHooks {

    private static final String TAG = "COPGVD-Tel";

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        if (!cfg.isSpoofEnabled()) return;

        Class<?> clazz = XposedHelpers.findClassIfExists(
                "android.telephony.TelephonyManager", lpparam.classLoader);
        if (clazz == null) return;

        final String iso     = cfg.getSimIso();
        final String carrier = cfg.getSimCarrier();
        final String mccmnc  = cfg.getSimMccMnc();

        if (!iso.isEmpty()) {
            try {
                XposedHelpers.findAndHookMethod(clazz, "getNetworkCountryIso",
                    new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(iso); }
                    });
                XposedHelpers.findAndHookMethod(clazz, "getSimCountryIso",
                    new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(iso); }
                    });
            } catch (Throwable t) { XposedBridge.log(TAG + ": ISO hooks: " + t.getMessage()); }
        }

        if (!carrier.isEmpty()) {
            try {
                XposedHelpers.findAndHookMethod(clazz, "getNetworkOperatorName",
                    new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(carrier); }
                    });
                XposedHelpers.findAndHookMethod(clazz, "getSimOperatorName",
                    new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(carrier); }
                    });
            } catch (Throwable t) { XposedBridge.log(TAG + ": Carrier hooks: " + t.getMessage()); }
        }

        if (!mccmnc.isEmpty()) {
            try {
                XposedHelpers.findAndHookMethod(clazz, "getNetworkOperator",
                    new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(mccmnc); }
                    });
                XposedHelpers.findAndHookMethod(clazz, "getSimOperator",
                    new XC_MethodHook() {
                        @Override
                        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(mccmnc); }
                    });
            } catch (Throwable t) { XposedBridge.log(TAG + ": MCC hooks: " + t.getMessage()); }
        }
    }
}
