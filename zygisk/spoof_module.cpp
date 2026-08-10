#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <json.hpp>
#include <fstream>
#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <sys/system_properties.h>
#include <unistd.h>

using json = nlohmann::json;

#define LOG_TAG "COPG-VD"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ERROR_LOG(...) LOGE("[ERROR] " __VA_ARGS__)

static const std::string config_file = "/data/adb/COPG-VD.json";
// What the ROM really is. NEVER a system property: the module rewrites those very props, so
// asking the system would be asking our own lie. /build.prop does not exist on these devices;
// on a custom ROM the fingerprint line inside this file is stale, but ro.build.version.* is good.
static const std::string rom_prop_file = "/system/build.prop";
static const std::string version_policy_file = "/data/adb/modules/COPG-VD/.spoof.version";

// The Android version belongs to the ROM, not to the build being spoofed. An app told the SDK
// is newer than the framework really is calls APIs that do not exist: Google's apps crash, the
// device reboots, and it repeats - a softloop, which leaves nothing in the boot logs.
//   Never = the version group is never applied (default)
//   Rom   = only what does not exceed the ROM (in practice, lowering the SDK)
//   Force = whatever the config says
enum class VersionPolicy { Never, Rom, Force };

struct RomVersion {
    std::string release;
    std::string codename;
    int sdk = 0;
};

struct DeviceInfo {
    std::string brand;
    std::string device;
    std::string manufacturer;
    std::string model;
    std::string fingerprint;
    std::string product;
    std::string android_version;
    int version_sdk_int = 0;
    std::string board;
    std::string bootloader;
    std::string hardware;
    std::string id;
    std::string display;
    std::string host;
    std::string odm_sku;
    std::string sku;
    std::string user;
    int64_t time = 0;
    std::string version_incremental;
    std::string version_sdk;
    int version_sdk_int_full = 0;
    std::string version_security_patch;
    std::string version_release_or_codename;
    std::string version_release_or_preview_display;
    std::string version_codename;
    // SIM / GPS / Baseband spoofing
    std::string sim_mccmnc, sim_country, sim_iso, sim_carrier;
    std::string gps_lat, gps_long, gps_timezone;
    std::string baseband_version, ram_gb, usb_debugging;
    std::string wifi_ssid, media_drm_id, media_drm_level;
    bool sim_spoof_enabled = false;
};

static inline std::string trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char c) { 
        return std::isspace(c); 
    });
    auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char c) { 
        return std::isspace(c); 
    }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

static RomVersion readRomVersion() {
    RomVersion rom;
    std::ifstream file(rom_prop_file);
    if (!file.is_open()) return rom;                 // unknown ROM -> nothing is allowed through
    std::string line;
    while (std::getline(file, line)) {
        auto take = [&line](const char* key, std::string& out) {
            const std::string needle = std::string(key) + "=";
            if (line.rfind(needle, 0) == 0) out = trim(line.substr(needle.size()));
        };
        take("ro.build.version.release", rom.release);
        take("ro.build.version.codename", rom.codename);
        std::string sdk;
        take("ro.build.version.sdk", sdk);
        if (!sdk.empty()) {
            try { rom.sdk = std::stoi(sdk); } catch (const std::exception&) { rom.sdk = 0; }
        }
    }
    return rom;
}

static VersionPolicy readVersionPolicy() {
    std::ifstream file(version_policy_file);
    if (!file.is_open()) return VersionPolicy::Never;
    std::string value;
    std::getline(file, value);
    value = trim(value);
    if (value == "force") return VersionPolicy::Force;
    if (value == "rom") return VersionPolicy::Rom;
    return VersionPolicy::Never;
}

// AOSP: RELEASE_OR_CODENAME = "REL".equals(CODENAME) ? RELEASE : CODENAME. Copying CODENAME
// into it publishes the literal string "REL" where the version number belongs, which is a
// combination no real device reports.
static std::string releaseOrCodename(const std::string& codename, const std::string& release) {
    return (codename.empty() || codename == "REL") ? release : codename;
}

class COPGVDModule : public zygisk::ModuleBase {
private:
    zygisk::Api* api = nullptr;
    JNIEnv* env = nullptr;

    // Value-initialized: setInt/setLong only skip a field when it is 0, so an
    // indeterminate int here would be written straight into Build.TIME.
    DeviceInfo spoof_info{};

    void spoofDevice() {
        jclass buildClass = env->FindClass("android/os/Build");
        if (!buildClass) {
            env->ExceptionClear();
            return;
        }

        auto getField = [this](jclass cls, const char* name, const char* sig) -> jfieldID {
            jfieldID id = env->GetStaticFieldID(cls, name, sig);
            if (env->ExceptionCheck()) env->ExceptionClear();
            return id;
        };

        jfieldID build_modelField = getField(buildClass, "MODEL", "Ljava/lang/String;");
        jfieldID build_brandField = getField(buildClass, "BRAND", "Ljava/lang/String;");
        jfieldID build_deviceField = getField(buildClass, "DEVICE", "Ljava/lang/String;");
        jfieldID build_manufacturerField = getField(buildClass, "MANUFACTURER", "Ljava/lang/String;");
        jfieldID build_fingerprintField = getField(buildClass, "FINGERPRINT", "Ljava/lang/String;");
        jfieldID build_productField = getField(buildClass, "PRODUCT", "Ljava/lang/String;");
        jfieldID build_boardField = getField(buildClass, "BOARD", "Ljava/lang/String;");
        jfieldID build_bootloaderField = getField(buildClass, "BOOTLOADER", "Ljava/lang/String;");
        jfieldID build_hardwareField = getField(buildClass, "HARDWARE", "Ljava/lang/String;");
        jfieldID build_idField = getField(buildClass, "ID", "Ljava/lang/String;");
        jfieldID build_displayField = getField(buildClass, "DISPLAY", "Ljava/lang/String;");
        jfieldID build_hostField = getField(buildClass, "HOST", "Ljava/lang/String;");
        jfieldID build_odm_skuField = getField(buildClass, "ODM_SKU", "Ljava/lang/String;");
        jfieldID build_skuField = getField(buildClass, "SKU", "Ljava/lang/String;");
        jfieldID build_tagsField = getField(buildClass, "TAGS", "Ljava/lang/String;");
        jfieldID build_timeField = getField(buildClass, "TIME", "J");
        jfieldID build_typeField = getField(buildClass, "TYPE", "Ljava/lang/String;");
        jfieldID build_userField = getField(buildClass, "USER", "Ljava/lang/String;");

        jclass versionClass = env->FindClass("android/os/Build$VERSION");
        jfieldID build_version_releaseField = nullptr;
        jfieldID build_version_sdk_intField = nullptr;
        jfieldID build_version_codenameField = nullptr;
        jfieldID build_version_incrementalField = nullptr;
        jfieldID build_version_sdkField = nullptr;
        jfieldID build_version_sdk_int_fullField = nullptr;
        jfieldID build_version_security_patchField = nullptr;
        jfieldID build_version_release_or_codenameField = nullptr;
        jfieldID build_version_release_or_preview_displayField = nullptr;

        if (versionClass) {
            build_version_releaseField = getField(versionClass, "RELEASE", "Ljava/lang/String;");
            build_version_sdk_intField = getField(versionClass, "SDK_INT", "I");
            build_version_codenameField = getField(versionClass, "CODENAME", "Ljava/lang/String;");
            build_version_incrementalField = getField(versionClass, "INCREMENTAL", "Ljava/lang/String;");
            build_version_sdkField = getField(versionClass, "SDK", "Ljava/lang/String;");
            build_version_sdk_int_fullField = getField(versionClass, "SDK_INT_FULL", "I");
            build_version_security_patchField = getField(versionClass, "SECURITY_PATCH", "Ljava/lang/String;");
            build_version_release_or_codenameField = getField(versionClass, "RELEASE_OR_CODENAME", "Ljava/lang/String;");
            build_version_release_or_preview_displayField = getField(versionClass, "RELEASE_OR_PREVIEW_DISPLAY", "Ljava/lang/String;");
        }

        std::ifstream file(config_file);
        if (!file.is_open()) {
            ERROR_LOG("Failed to open: %s", config_file.c_str());
            env->DeleteLocalRef(buildClass);
            if (versionClass) env->DeleteLocalRef(versionClass);
            return;
        }

        try {
            json config = json::parse(file);

            if (config.contains(std::string(LOG_TAG)) && config[LOG_TAG].is_object()) {
                auto device = config[LOG_TAG];

                spoof_info.brand = device.value("BRAND", "");
                spoof_info.device = device.value("DEVICE", "");
                spoof_info.manufacturer = device.value("MANUFACTURER", "");
                spoof_info.model = device.value("MODEL", "");
                spoof_info.fingerprint = device.value("FINGERPRINT", "");
                spoof_info.product = device.value("PRODUCT", "");
                spoof_info.board = device.value("BOARD", "");
                spoof_info.bootloader = device.value("BOOTLOADER", "");
                spoof_info.hardware = device.value("HARDWARE", "");
                spoof_info.id = device.value("ID", "");
                spoof_info.display = device.value("DISPLAY", "");
                spoof_info.host = device.value("HOST", "");
                spoof_info.odm_sku = device.value("ODM_SKU", spoof_info.product);
                spoof_info.sku = device.value("SKU", spoof_info.hardware);
                spoof_info.user = device.value("USER", "");
                spoof_info.version_incremental = device.value("INCREMENTAL", "");
                spoof_info.version_security_patch = device.value("SECURITY_PATCH", "");
                spoof_info.baseband_version = device.value("BASEBAND", "");
                spoof_info.ram_gb = device.value("RAM_GB", "12");
                spoof_info.usb_debugging = device.value("USB_DEBUGGING", "0");
                if (config.contains("COPG-VD-SimGps") && config["COPG-VD-SimGps"].is_object()) {
                    auto& sg = config["COPG-VD-SimGps"];
                    spoof_info.sim_spoof_enabled = sg.value("SIM_SPOOF_ENABLED", false);
                    spoof_info.sim_mccmnc = sg.value("SIM_MCCMNC", "");
                    spoof_info.sim_country = sg.value("SIM_COUNTRY", "");
                    spoof_info.sim_iso = sg.value("SIM_ISO", "");
                    spoof_info.sim_carrier = sg.value("SIM_CARRIER", "");
                    spoof_info.gps_lat = sg.value("GPS_LAT", "");
                    spoof_info.gps_long = sg.value("GPS_LONG", "");
                    spoof_info.gps_timezone = sg.value("GPS_TIMEZONE", "");
                    spoof_info.wifi_ssid = sg.value("WIFI_SSID", "");
                                    spoof_info.media_drm_id = sg.value("MEDIA_DRM_ID", "");
                spoof_info.media_drm_level = sg.value("MEDIA_DRM_LEVEL", "L3");
                }
                if (device.contains("TIMESTAMP")) {
                    const auto& device_timestamp = device["TIMESTAMP"];
                    spoof_info.time = std::stoll(device_timestamp.get<std::string>()) * 1000;
                }

                // --- the version group, and only what the semaphore lets through ---
                const RomVersion rom = readRomVersion();
                const VersionPolicy policy = readVersionPolicy();
                auto allowed = [&rom, policy](const char* field, const std::string& value) {
                    if (policy == VersionPolicy::Force) return true;
                    if (policy == VersionPolicy::Never) return false;
                    // Rom: never above the ROM. Raising the SDK is what makes apps call APIs
                    // the framework does not have; lowering it only makes them ask for less.
                    const std::string f(field);
                    if (f == "SDK_INT" || f == "SDK_FULL") {
                        if (rom.sdk == 0) return false;
                        try { return std::stoi(value) <= rom.sdk; }
                        catch (const std::exception&) { return false; }
                    }
                    if (f == "ANDROID_VERSION") return !rom.release.empty() && value == rom.release;
                    if (f == "CODENAME") return !rom.codename.empty() && value == rom.codename;
                    return false;
                };

                const std::string cfg_codename = device.value("CODENAME", "");
                if (!trim(cfg_codename).empty() && allowed("CODENAME", cfg_codename)) {
                    spoof_info.version_codename = cfg_codename;
                }

                if (device.contains("ANDROID_VERSION")) {
                    const std::string value = device["ANDROID_VERSION"].get<std::string>();
                    if (allowed("ANDROID_VERSION", value)) spoof_info.android_version = value;
                }

                if (device.contains("SDK_INT")) {
                    const std::string value = device["SDK_INT"].get<std::string>();
                    if (allowed("SDK_INT", value)) {
                        spoof_info.version_sdk_int = std::stoi(value);
                        spoof_info.version_sdk = std::to_string(spoof_info.version_sdk_int);
                    }
                }

                if (device.contains("SDK_FULL")) {
                    const std::string value = device["SDK_FULL"].get<std::string>();
                    if (allowed("SDK_FULL", value)) {
                        auto dot_position = value.find('.');
                        int major = std::stoi(dot_position == std::string::npos ? value : value.substr(0, dot_position));
                        int minor = 0;
                        if (dot_position != std::string::npos) {
                            minor = std::stoi(value.substr(dot_position + 1));
                        }
                        spoof_info.version_sdk_int_full = major * 100000 + minor;
                    }
                }
                if (!spoof_info.version_sdk_int_full && spoof_info.version_sdk_int) {
                    spoof_info.version_sdk_int_full = spoof_info.version_sdk_int * 100000;
                }

                // Derived from what actually got through, by the AOSP rule. Left empty when the
                // version is not spoofed at all, so the framework keeps its own correct values.
                if (!spoof_info.version_codename.empty() || !spoof_info.android_version.empty()) {
                    const std::string cod = spoof_info.version_codename.empty()
                                          ? rom.codename : spoof_info.version_codename;
                    const std::string rel = spoof_info.android_version.empty()
                                          ? rom.release : spoof_info.android_version;
                    spoof_info.version_release_or_codename = releaseOrCodename(cod, rel);
                    spoof_info.version_release_or_preview_display = spoof_info.version_release_or_codename;
                }
            }
        } catch (const std::exception& e) {
            ERROR_LOG("Config error: %s", e.what());
            env->DeleteLocalRef(buildClass);
            if (versionClass) env->DeleteLocalRef(versionClass);
            return;
        }

        auto setStr = [this](jclass thisClass, jfieldID field, const std::string& value) {
            if (!field || trim(value).empty()) return;
            jstring js = env->NewStringUTF(value.c_str());
            if (!js || env->ExceptionCheck()) {
                env->ExceptionClear();
                return;
            }
            env->SetStaticObjectField(thisClass, field, js);
            env->DeleteLocalRef(js);
            if (env->ExceptionCheck()) env->ExceptionClear();
        };

        auto setInt = [this](jclass thisClass, jfieldID field, int value) {
            if (!field || value == 0) return;
            env->SetStaticIntField(thisClass, field, value);
            if (env->ExceptionCheck()) env->ExceptionClear();
        };

        auto setLong = [this](jclass thisClass, jfieldID field, int64_t value) {
            if (!field || value == 0) return;
            env->SetStaticLongField(thisClass, field, value);
            if (env->ExceptionCheck()) env->ExceptionClear();
        };

        setStr(buildClass, build_modelField, spoof_info.model);
        setStr(buildClass, build_brandField, spoof_info.brand);
        setStr(buildClass, build_deviceField, spoof_info.device);
        setStr(buildClass, build_manufacturerField, spoof_info.manufacturer);
        setStr(buildClass, build_fingerprintField, spoof_info.fingerprint);
        setStr(buildClass, build_productField, spoof_info.product);
        setStr(buildClass, build_boardField, spoof_info.board);
        setStr(buildClass, build_bootloaderField, spoof_info.bootloader);
        setStr(buildClass, build_hardwareField, spoof_info.hardware);
        setStr(buildClass, build_idField, spoof_info.id);
        setStr(buildClass, build_displayField, spoof_info.display);
        setStr(buildClass, build_hostField, spoof_info.host);
        setStr(buildClass, build_odm_skuField, spoof_info.odm_sku);
        setStr(buildClass, build_skuField, spoof_info.sku);
        setStr(buildClass, build_userField, spoof_info.user);
        setLong(buildClass, build_timeField, spoof_info.time);
        setStr(buildClass, build_tagsField, "release-keys");
        setStr(buildClass, build_typeField, "user");

        if (versionClass) {
            setStr(versionClass, build_version_codenameField, spoof_info.version_codename);
            setStr(versionClass, build_version_incrementalField, spoof_info.version_incremental);
            setStr(versionClass, build_version_sdkField, spoof_info.version_sdk);
            setInt(versionClass, build_version_sdk_int_fullField, spoof_info.version_sdk_int_full);
            setStr(versionClass, build_version_security_patchField, spoof_info.version_security_patch);
            setStr(versionClass, build_version_releaseField, spoof_info.android_version);
            setInt(versionClass, build_version_sdk_intField, spoof_info.version_sdk_int);
            setStr(versionClass, build_version_release_or_codenameField, spoof_info.version_release_or_codename);
            setStr(versionClass, build_version_release_or_preview_displayField, spoof_info.version_release_or_preview_display);
        }

        // === SIM / GPS / Baseband / Wi-Fi / MediaDrm spoofing ===
        if (spoof_info.sim_spoof_enabled) {
            auto sp = [](const char* k, const std::string& v) { if(!v.empty()) __system_property_set(k, v.c_str()); };
            // Baseband
            sp("gsm.version.baseband", spoof_info.baseband_version);
            sp("ro.baseband", spoof_info.baseband_version);
            // SIM operator props
            sp("gsm.sim.operator.alpha", spoof_info.sim_carrier);
            sp("gsm.sim.operator.numeric", spoof_info.sim_mccmnc);
            sp("gsm.sim.operator.iso-country", spoof_info.sim_iso);
            sp("gsm.operator.alpha", spoof_info.sim_carrier);
            sp("gsm.operator.numeric", spoof_info.sim_mccmnc);
            sp("gsm.operator.iso-country", spoof_info.sim_iso);
            sp("ro.carrier", spoof_info.sim_mccmnc);
            sp("ro.com.google.clientidbase", spoof_info.sim_mccmnc);
            // Country / ISO
            sp("persist.radio.country_code", spoof_info.sim_iso);
            sp("persist.radio.country_iso", spoof_info.sim_iso);
            sp("ro.product.locale.region", spoof_info.sim_iso);
            sp("persist.sys.country", spoof_info.sim_iso);
            // Samsung CSC
            sp("ro.csc.country_code", spoof_info.sim_iso);
            sp("ro.csc.countryiso_code", spoof_info.sim_iso);
            sp("ro.csc.sales_code", spoof_info.sim_iso);
            sp("ril.sales_code", spoof_info.sim_iso);
            sp("ril.serialnumber", std::string("R5C") + spoof_info.sim_mccmnc + "00000");
            // GPS
            if (!spoof_info.gps_lat.empty() && !spoof_info.gps_long.empty()) {
                sp("persist.sys.loc.lat", spoof_info.gps_lat);
                sp("persist.sys.loc.lng", spoof_info.gps_long);
            }
            sp("persist.sys.timezone", spoof_info.gps_timezone);
            // USB Debugging
            if (spoof_info.usb_debugging == "1") {
                __system_property_set("persist.sys.usb.config", "adb");
                __system_property_set("sys.usb.config", "adb");
                __system_property_set("sys.usb.state", "adb");
            }
            // ro.hardware.chipname (Samsung)
            if (!spoof_info.baseband_version.empty()) {
                std::string chip = spoof_info.baseband_version;
                size_t dash = chip.find('-');
                if (dash != std::string::npos) chip = chip.substr(0, dash);
                sp("ro.hardware.chipname", chip);
            }
            // SoC
            sp("ro.soc.manufacturer", spoof_info.manufacturer);
            sp("ro.soc.model", spoof_info.hardware);
            // Airplane mode off
            sp("persist.sys.airplane_mode", "off");
            __system_property_set("persist.radio.airplane_mode_on", "0");
            // Wi-Fi
            if (!spoof_info.wifi_ssid.empty()) {
                sp("net.hostname", spoof_info.wifi_ssid);
                sp("persist.sys.wifi_ssid", spoof_info.wifi_ssid);
            }
            // MediaDrm / Widevine
            if (!spoof_info.media_drm_id.empty()) {
                sp("media.drm.id", spoof_info.media_drm_id);
                sp("persist.sys.media_drm_id", spoof_info.media_drm_id);
                // Widevine security level
                if (spoof_info.media_drm_level == "L1") {
                    sp("media.drm.security.level", "L1");
                    sp("persist.sys.widevine_level", "L1");
                } else if (spoof_info.media_drm_level == "L2") {
                    sp("media.drm.security.level", "L2");
                    sp("persist.sys.widevine_level", "L2");
                } else {
                    sp("media.drm.security.level", "L3");
                    sp("persist.sys.widevine_level", "L3");
                }
            }
            // /proc/cpuinfo Hardware spoofing
            sp("ro.board.platform", spoof_info.board);
            sp("ro.chipname", spoof_info.hardware);
            sp("ro.hardware", spoof_info.hardware);
            // /proc/meminfo RAM spoofing
            if (!spoof_info.ram_gb.empty() && spoof_info.ram_gb != "12") {
                sp("ro.config.totalmem", spoof_info.ram_gb);
                sp("persist.sys.ram_size", spoof_info.ram_gb);
            }
        }
        
        env->DeleteLocalRef(buildClass);
        if (versionClass) env->DeleteLocalRef(versionClass);
    }

public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;

        spoofDevice();

        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }
};

REGISTER_ZYGISK_MODULE(COPGVDModule)
