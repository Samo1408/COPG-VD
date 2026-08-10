package com.copgvd.xposed;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import de.robv.android.xposed.XposedBridge;

/**
 * Sets static final Build fields via reflection.
 * Called from system_server context (IXposedHookLoadPackage for android package).
 */
public class BuildHooks {

    static boolean setStaticField(Class<?> clazz, String fieldName, String value) {
        if (value.isEmpty()) return false;
        try {
            Field f = clazz.getDeclaredField(fieldName);
            f.setAccessible(true);
            Field m = Field.class.getDeclaredField("accessFlags");
            m.setAccessible(true);
            m.setInt(f, f.getModifiers() & ~Modifier.FINAL);
            f.set(null, value);
            m.setInt(f, f.getModifiers() | Modifier.FINAL);
            return true;
        } catch (Throwable t) {
            XposedBridge.log("COPGVD-Build: " + fieldName + " failed: " + t.getMessage());
            return false;
        }
    }
}
