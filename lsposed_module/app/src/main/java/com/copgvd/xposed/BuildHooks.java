package com.copgvd.xposed;

import android.os.Build;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;

public class BuildHooks {

    // Config key -> Build field name
    static final String[][] MAP = {
        {"MODEL","MODEL"},{"MANUFACTURER","MANUFACTURER"},{"BRAND","BRAND"},
        {"PRODUCT","PRODUCT"},{"DEVICE","DEVICE"},{"BOARD","BOARD"},
        {"HARDWARE","HARDWARE"},{"FINGERPRINT","FINGERPRINT"},
        {"ID","ID"},{"DISPLAY","DISPLAY"},{"HOST","HOST"},
        {"USER","USER"},{"TAGS","TAGS"},{"SKU","SKU"},
    };

    public static void install(XC_LoadPackage.LoadPackageParam lpparam) {
        SpoofConfig cfg = SpoofConfig.get();
        boolean any = false;

        for (String[] pair : MAP) {
            String v = cfg.getDeviceField(pair[0], "");
            if (!v.isEmpty() && setField(Build.class, pair[1], v)) any = true;
        }

        // VERSION fields
        setField(Build.VERSION.class, "RELEASE", cfg.getDeviceField("ANDROID_VER", ""));
        setField(Build.VERSION.class, "INCREMENTAL", cfg.getDeviceField("INCREMENTAL", ""));
        setField(Build.VERSION.class, "SECURITY_PATCH", cfg.getDeviceField("SECURITY_PATCH", ""));

        String sdk = cfg.getDeviceField("SDK", "");
        if (!sdk.isEmpty()) setIntField(Build.VERSION.class, "SDK_INT", Integer.parseInt(sdk));

        setField(Build.class, "TYPE", cfg.getDeviceField("TYPE", ""));

        if (any) XposedBridge.log("COPGVD-Build: build fields hooked");
    }

    static boolean setField(Class<?> c, String name, String val) {
        if (val.isEmpty()) return false;
        try {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            Field mf = Field.class.getDeclaredField("accessFlags");
            mf.setAccessible(true);
            mf.setInt(f, f.getModifiers() & ~Modifier.FINAL);
            f.set(null, val);
            mf.setInt(f, f.getModifiers() | Modifier.FINAL);
            return true;
        } catch (Exception e) {
            XposedBridge.log("COPGVD-Build: failed " + name + ": " + e.getMessage());
            return false;
        }
    }

    static boolean setIntField(Class<?> c, String name, int val) {
        try {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            Field mf = Field.class.getDeclaredField("accessFlags");
            mf.setAccessible(true);
            mf.setInt(f, f.getModifiers() & ~Modifier.FINAL);
            f.setInt(null, val);
            mf.setInt(f, f.getModifiers() | Modifier.FINAL);
            return true;
        } catch (Exception e) { return false; }
    }
}
