// SemSystemProperties hooks for Samsung devices
// android.os.SemSystemProperties
// Hooked via Xposed/LSPosed style JNI

#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <fstream>
#include <android/log.h>
#include <algorithm>
#include <sys/system_properties.h>

#define LOG_TAG "COPG-VD-SemSys"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::string sim_iso;
static std::string sim_country;
static std::string sim_carrier;
static std::string sim_mccmnc;
static std::string baseband;
static std::string manufacturer;
static bool enabled = false;

static void loadSemConfig() {
    std::ifstream file("/data/adb/COPG-VD.json");
    if (!file.is_open()) return;
    try {
        auto config = nlohmann::json::parse(file);
        if (config.contains("COPG-VD-SimGps") && config["COPG-VD-SimGps"].is_object()) {
            auto& sg = config["COPG-VD-SimGps"];
            enabled = sg.value("SIM_SPOOF_ENABLED", false);
            sim_iso = sg.value("SIM_ISO", "");
            sim_country = sg.value("SIM_COUNTRY", "");
            sim_carrier = sg.value("SIM_CARRIER", "");
            sim_mccmnc = sg.value("SIM_MCCMNC", "");
        }
        if (config.contains("COPG-VD") && config["COPG-VD"].is_object()) {
            auto& dev = config["COPG-VD"];
            baseband = dev.value("BASEBAND", "");
            manufacturer = dev.value("MANUFACTURER", "");
        }
    } catch (...) {}
}

// Helper to set sysprop
static void sp(const char* k, const std::string& v) {
    if (!v.empty()) __system_property_set(k, v.c_str());
}

// Called from spoofDevice or via separate preload
extern "C" void applySemSystemHooks() {
    loadSemConfig();
    if (!enabled) return;
    
    sp("ro.csc.country_code", sim_iso);
    sp("ro.csc.countryiso_code", sim_iso);
    sp("ro.csc.sales_code", sim_iso);
    sp("ril.sales_code", sim_iso);
    sp("persist.sys.country", sim_iso);
    sp("ro.hardware.chipname", baseband);
    sp("ro.baseband", baseband);
    sp("ril.serialnumber", "R5C" + sim_mccmnc + "00000");
}

// Country code lookup table
static const char* getCountryCode(const std::string& iso) {
    if (iso == "us") return "US";
    if (iso == "gb") return "GB";
    if (iso == "de") return "DE";
    if (iso == "fr") return "FR";
    if (iso == "sa") return "SA";
    if (iso == "ae") return "AE";
    if (iso == "tr") return "TR";
    if (iso == "jp") return "JP";
    if (iso == "cn") return "CN";
    if (iso == "in") return "IN";
    if (iso == "mx") return "MX";
    if (iso == "br") return "BR";
    if (iso == "ar") return "AR";
    if (iso == "au") return "AU";
    if (iso == "kr") return "KR";
    if (iso == "ca") return "CA";
    if (iso == "ru") return "RU";
    if (iso == "eg") return "EG";
    return "US";
}

extern "C" const char* getCountryIsoHook() {
    if (!enabled) return nullptr;
    static std::string result;
    result = sim_iso;
    return result.c_str();
}

extern "C" const char* getCountryCodeHook() {
    if (!enabled) return nullptr;
    static std::string result;
    result = getCountryCode(sim_iso);
    return result.c_str();
}

extern "C" const char* getSalesCodeHook() {
    if (!enabled) return nullptr;
    static std::string result;
    result = sim_iso;
    return result.c_str();
}
