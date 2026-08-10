package com.copgvd.xposed;

import android.util.Log;
import de.robv.android.xposed.XposedBridge;
import org.json.JSONObject;
import java.io.File;
import java.io.FileReader;
import java.io.BufferedReader;

/**
 * Reads COPG-VD config from correct path: /data/adb/modules/COPG-VD/webroot/config.json
 * This is where the Zygisk module/WebUI writes the actual config.
 */
public class SpoofConfig {

    private static final String TAG = "COPGVD-Config";

    // Try multiple paths - Zygisk module writes here
    private static final String[] CONFIG_PATHS = {
        "/data/adb/modules/COPG-VD/webroot/config.json",
        "/data/adb/COPG-VD.json",
    };

    private static SpoofConfig instance;
    private JSONObject config = null;
    private JSONObject device = null;
    private JSONObject simGps = null;
    private boolean spoofEnabled = false;
    private String simIso = "";
    private String simCarrier = "";
    private String simMccMnc = "";
    private String wifiSsid = "";
    private String drmId = "";
    private String drmLevel = "L3";

    private SpoofConfig() {}

    public static synchronized SpoofConfig get() {
        if (instance == null) {
            instance = new SpoofConfig();
            instance.reload();
        }
        return instance;
    }

    public synchronized void reload() {
        for (String path : CONFIG_PATHS) {
            File f = new File(path);
            if (f.exists()) {
                try (BufferedReader br = new BufferedReader(new FileReader(f))) {
                    StringBuilder sb = new StringBuilder();
                    String line;
                    while ((line = br.readLine()) != null) sb.append(line);
                    config = new JSONObject(sb.toString());
                    parseConfig();
                    XposedBridge.log(TAG + ": Loaded from " + path + " spoof=" + spoofEnabled
                        + " simIso=" + simIso + " wifi=" + wifiSsid + " drm=" + drmId);
                    return;
                } catch (Exception e) {
                    XposedBridge.log(TAG + ": Error reading " + path + ": " + e.getMessage());
                }
            }
        }
        XposedBridge.log(TAG + ": No config found at any path");
    }

    private void parseConfig() {
        if (config == null) return;

        // Device profile - try COPG-VD-Profile array first
        device = null;
        if (config.has("COPG-VD-Profile")) {
            try {
                org.json.JSONArray arr = config.getJSONArray("COPG-VD-Profile");
                if (arr.length() > 0) {
                    device = arr.getJSONObject(0);
                }
            } catch (Exception e) {}
        }
        // Fallback: COPG-VD-Devices
        if (device == null && config.has("COPG-VD-Devices")) {
            try {
                String sel = config.optString("COPG-VD-Selected", "");
                JSONObject devices = config.getJSONObject("COPG-VD-Devices");
                if (!sel.isEmpty() && devices.has(sel)) {
                    device = devices.getJSONObject(sel);
                }
            } catch (Exception e) {}
        }

        // SIM/GPS config
        simGps = null;
        if (config.has("COPG-VD-SimGps")) {
            try {
                simGps = config.getJSONObject("COPG-VD-SimGps");
                spoofEnabled = simGps.optBoolean("SIM_SPOOF_ENABLED", false);
                simIso = simGps.optString("SIM_ISO", "");
                simCarrier = simGps.optString("SIM_CARRIER", "");
                simMccMnc = simGps.optString("SIM_MCCMNC", "");
                wifiSsid = simGps.optString("WIFI_SSID", "");
                drmId = simGps.optString("MEDIA_DRM_ID", "");
                drmLevel = simGps.optString("MEDIA_DRM_LEVEL", "L3");
            } catch (Exception e) {}
        }
    }

    public boolean isSpoofEnabled() { return spoofEnabled; }
    public String getSimIso()     { return simIso; }
    public String getSimCarrier() { return simCarrier; }
    public String getSimMccMnc()  { return simMccMnc; }
    public String getWifiSsid()   { return wifiSsid; }
    public String getMediaDrmId() { return drmId; }
    public String getMediaDrmLevel() { return drmLevel; }

    public String getDeviceField(String key, String def) {
        if (device == null) return def;
        return device.optString(key, def);
    }
    public String getSimGpsField(String key, String def) {
        if (simGps == null) return def;
        return simGps.optString(key, def);
    }
}
