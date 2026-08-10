package com.copgvd.xposed;

import android.util.Log;
import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;
import de.robv.android.xposed.XposedHelpers;

/**
 * LSPosed entry point - hooks ALL app processes, not just system_server.
 * Key fix: use IXposedHookLoadPackage to hook EVERY app that loads,
 * not just the "android" package.
 */
public class COPGVDXposed implements IXposedHookZygoteInit, IXposedHookLoadPackage {

    private static final String TAG = "COPGVD";

    @Override
    public void initZygote(StartupParam param) {
        SpoofConfig.get().reload();
        XposedBridge.log("COPG-VD: initZygote - config loaded: " + SpoofConfig.get().isSpoofEnabled());
    }

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        // Don't hook our own app
        if (lpparam.packageName.equals("com.copgvd.xposed")) return;

        // Reload config periodically (first app load after boot)
        SpoofConfig.get().reload();

        // Always install hooks for ALL apps (not just system_server)
        // This is the KEY FIX: hooks must be in every process that reads device info
        installHooks(lpparam);
    }

    private void installHooks(XC_LoadPackage.LoadPackageParam lpparam) {
        if (!SpoofConfig.get().isSpoofEnabled()) return;

        try {
            TelephonyHooks.install(lpparam);
        } catch (Throwable t) { XposedBridge.log(TAG + ": Telephony err: " + t.getMessage()); }

        try {
            WifiHooks.install(lpparam);
        } catch (Throwable t) { XposedBridge.log(TAG + ": WiFi err: " + t.getMessage()); }

        try {
            MediaDrmHooks.install(lpparam);
        } catch (Throwable t) { XposedBridge.log(TAG + ": MediaDrm err: " + t.getMessage()); }

        try {
            BuildHooks.install(lpparam);
        } catch (Throwable t) { XposedBridge.log(TAG + ": Build err: " + t.getMessage()); }
    }
}
