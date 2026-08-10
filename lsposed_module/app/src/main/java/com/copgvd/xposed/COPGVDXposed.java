package com.copgvd.xposed;

import android.util.Log;
import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class COPGVDXposed implements IXposedHookZygoteInit, IXposedHookLoadPackage {

    private static final String TAG = "COPGVD-Xposed";

    @Override
    public void initZygote(StartupParam param) {
        SpoofConfig.get().reload();
        Log.i(TAG, "COPG-VD LSPosed initialized in zygote");
    }

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        if ("android".equals(lpparam.packageName)) {
            Log.i(TAG, "Hooking system framework");
            SpoofConfig.get().reload();
            if (!SpoofConfig.get().isSpoofEnabled()) return;
            BuildHooks.install(lpparam);
            TelephonyHooks.install(lpparam);
            WifiHooks.install(lpparam);
            MediaDrmHooks.install(lpparam);
            Log.i(TAG, "All hooks installed");
        }
    }
}
