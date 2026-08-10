package com.copgvd.xposed;

import android.os.Build;
import android.util.Log;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class BuildHooks {

    private static final String TAG = "COPGVD-Build";

    private static final String[][] FIELD_MAP = {
        {"MODEL","MODEL"},{"MANUFACTURER","MANUFACTURER"},{"BRAND","BRAND"},
        {"PRODUCT","PRODUCT"},{"DEVICE","DEVICE"},{"BOARD","BOARD"},
        {"HARDWARE","HARDWARE"},{"BOOTLOADER","BOOTLOADER"},{"FINGERPRINT","FINGERPRINT"},
        {"ID","ID"},{"DISPLAY","DISPLAY"},{"HOST","HOST"},
        {"USER","USER"},{"TAGS","TAGS"},{"SKU","SKU"},{"ODM_SKU","ODM_SKU"},
    };

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();

        for (String[] pair : FIELD_MAP) {
            String val = cfg.getDeviceField(pair[0], "");
            if (!val.isEmpty()) setStaticFinalField(Build.class, pair[1], val);
        }

        // VERSION fields
        setStaticFinalField(Build.VERSION.class, "RELEASE",
                cfg.getDeviceField("ANDROID_VER", ""));
        setStaticFinalField(Build.VERSION.class, "INCREMENTAL",
                cfg.getDeviceField("INCREMENTAL", ""));
        setStaticFinalField(Build.VERSION.class, "SECURITY_PATCH",
                cfg.getDeviceField("SECURITY_PATCH", ""));

        // TYPE on Build itself
        setStaticFinalField(Build.class, "TYPE", cfg.getDeviceField("TYPE", ""));

        // SDK_INT (int)
        String sdk = cfg.getDeviceField("SDK", "");
        if (!sdk.isEmpty()) setStaticFinalIntField(Build.VERSION.class, "SDK_INT", Integer.parseInt(sdk));

        Log.i(TAG, "Build hooks installed");
    }

    private static void setStaticFinalField(Class<?> clazz, String name, String val) {
        if (val.isEmpty()) return;
        try {
            Field f = clazz.getDeclaredField(name);
            f.setAccessible(true);
            Field mf = Field.class.getDeclaredField("accessFlags");
            mf.setAccessible(true);
            mf.setInt(f, f.getModifiers() & ~Modifier.FINAL);
            f.set(null, val);
            mf.setInt(f, f.getModifiers() | Modifier.FINAL);
        } catch (Exception e) { Log.e(TAG, "Build field failed: " + name, e); }
    }

    private static void setStaticFinalIntField(Class<?> clazz, String name, int val) {
        try {
            Field f = clazz.getDeclaredField(name);
            f.setAccessible(true);
            Field mf = Field.class.getDeclaredField("accessFlags");
            mf.setAccessible(true);
            mf.setInt(f, f.getModifiers() & ~Modifier.FINAL);
            f.setInt(null, val);
            mf.setInt(f, f.getModifiers() | Modifier.FINAL);
        } catch (Exception e) { Log.e(TAG, "Build int field failed: " + name, e); }
    }
}
