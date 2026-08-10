package com.copgvd.xposed;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * COPG-VD LSPosed Module
 * 
 * ARCHITECTURE (same as vpnhide):
 * - Scope: System Framework only
 * - Hooks system_server services that apps query for device info
 * - Apps receive spoofed data via normal Binder IPC - NO hooks in app process
 * - Zero footprint in target apps
 */
public class COPGVDXposed implements IXposedHookZygoteInit, IXposedHookLoadPackage {

    private static boolean hooksInstalled = false;

    @Override
    public void initZygote(StartupParam param) {
        XposedBridge.log("COPG-VD: Zygote init");
    }

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        // Hook ONLY in system_server (process name "android" with package "android")
        if (!"android".equals(lpparam.processName) && !"system_server".equals(lpparam.processName)) {
            return;
        }
        
        // Install once
        if (hooksInstalled) return;
        hooksInstalled = true;

        XposedBridge.log("COPG-VD: Hooking in system_server");
        SpoofConfig.get().reload();
        
        if (!SpoofConfig.get().isSpoofEnabled()) {
            XposedBridge.log("COPG-VD: Spoofing disabled, no hooks");
            return;
        }

        // Hook system services that apps use to read device info
        SystemServiceHooks.install(lpparam);
        XposedBridge.log("COPG-VD: All system_server hooks installed");
    }
}
