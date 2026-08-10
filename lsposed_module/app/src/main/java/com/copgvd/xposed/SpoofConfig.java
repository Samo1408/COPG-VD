package com.copgvd.xposed;

import android.util.Log;
import org.json.JSONObject;
import org.json.JSONException;
import java.io.File;
import java.io.FileReader;
import java.io.BufferedReader;
import java.io.IOException;

public class SpoofConfig {

    private static final String TAG = "COPGVD-Xposed";
    private static final String CONFIG_PATH = "/data/adb/COPG-VD.json";

    private static SpoofConfig instance;
    private JSONObject config = null;
    private JSONObject device = null;
    private JSONObject simGps = null;

    private SpoofConfig() {}

    public static synchronized SpoofConfig get() {
        if (instance == null) { instance = new SpoofConfig(); instance.reload(); }
        return instance;
    }

    public synchronized void reload() {
        File f = new File(CONFIG_PATH);
        if (!f.exists()) return;
        try (BufferedReader br = new BufferedReader(new FileReader(f))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = br.readLine()) != null) sb.append(line);
            config = new JSONObject(sb.toString());
            String selected = config.optString("COPG-VD-Selected", "");
            if (!selected.isEmpty() && config.has("COPG-VD-Devices")) {
                JSONObject devices = config.getJSONObject("COPG-VD-Devices");
                if (devices.has(selected)) device = devices.getJSONObject(selected);
            }
            if (config.has("COPG-VD-SimGps")) simGps = config.getJSONObject("COPG-VD-SimGps");
            Log.i(TAG, "Config loaded: device=" + (device != null) + ", simGps=" + (simGps != null));
        } catch (Exception e) { Log.e(TAG, "Config load failed", e); }
    }

    public boolean isSpoofEnabled() { return simGps != null && simGps.optBoolean("SIM_SPOOF_ENABLED", false); }
    public String getDeviceField(String key, String def) { return device != null ? device.optString(key, def) : def; }
    public String getSimGpsField(String key, String def) { return simGps != null ? simGps.optString(key, def) : def; }
    public String getSimIso()     { return getSimGpsField("SIM_ISO", ""); }
    public String getSimCarrier() { return getSimGpsField("SIM_CARRIER", ""); }
    public String getSimMccMnc()  { return getSimGpsField("SIM_MCCMNC", ""); }
    public String getMcc() { String m = getSimMccMnc(); return m.length() >= 3 ? m.substring(0, 3) : ""; }
    public String getMnc() { String m = getSimMccMnc(); return m.length() >= 5 ? m.substring(3) : ""; }
    public String getWifiSsid()   { return getSimGpsField("WIFI_SSID", ""); }
    public String getMediaDrmId() { return getSimGpsField("MEDIA_DRM_ID", ""); }
    public String getMediaDrmLevel() { return getSimGpsField("MEDIA_DRM_LEVEL", "L3"); }
    public String getGpsLat()  { return getSimGpsField("GPS_LAT", ""); }
    public String getGpsLng()  { return getSimGpsField("GPS_LONG", ""); }
}
