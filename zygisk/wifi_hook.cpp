// WiFi SSID Hooking via JNI
// Hooks android.net.wifi.WifiManager.getConnectionInfo() to return spoofed SSID
#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <fstream>
#include <android/log.h>
#include <json.hpp>

#define LOG_TAG "COPG-VD-WiFi"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::string spoofed_ssid;

// Load spoofed SSID from config
static void loadWiFiSpoofConfig() {
    std::ifstream file("/data/adb/COPG-VD.json");
    if (!file.is_open()) return;
    try {
        auto config = nlohmann::json::parse(file);
        if (config.contains("COPG-VD-SimGps") && config["COPG-VD-SimGps"].is_object()) {
            auto& sg = config["COPG-VD-SimGps"];
            spoofed_ssid = sg.value("WIFI_SSID", "");
        }
    } catch (...) {}
}

// Original method pointer
static jstring (*orig_getSSID)(JNIEnv*, jobject) = nullptr;

// Hooked native getSSID implementation
static jstring hooked_getSSID(JNIEnv* env, jobject thiz) {
    loadWiFiSpoofConfig();
    if (!spoofed_ssid.empty()) {
        return env->NewStringUTF(spoofed_ssid.c_str());
    }
    // Fallback to original
    if (orig_getSSID) {
        return orig_getSSID(env, thiz);
    }
    return env->NewStringUTF("");
}

// Main hook installer - called from spoof_module.cpp
extern "C" void installWiFiHook(JNIEnv* env, zygisk::Api* api) {
    loadWiFiSpoofConfig();
    if (spoofed_ssid.empty()) return;
    
    // Hook WifiInfo.getSSID() - this is the native method behind WifiManager.getConnectionInfo().getSSID()
    JNINativeMethod methods[] = {
        {"getSSID", "()Ljava/lang/String;", (void*)hooked_getSSID}
    };
    
    api->hookJniNativeMethods(env, "android/net/wifi/WifiInfo", methods, 1);
    orig_getSSID = (jstring(*)(JNIEnv*,jobject)) methods[0].fnPtr;
    
    LOGE("WiFi SSID hook installed: %s", spoofed_ssid.c_str());
}
