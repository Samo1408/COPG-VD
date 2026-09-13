#include <jni.h>
#include <string>
#include <zygisk.hpp>
#include <json.hpp>
#include <fstream>
#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>
#include <lsplant.hpp>

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

namespace copg_lsplant { bool init(JNIEnv* env); }
namespace copg_device_hooks { void install(JNIEnv* env); }

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


static std::string packageFromNiceName(JNIEnv* env, jstring niceName) {
    if (!env || !niceName) return {};
    const char* chars = env->GetStringUTFChars(niceName, nullptr);
    if (!chars) return {};
    std::string name(chars);
    env->ReleaseStringUTFChars(niceName, chars);
    const auto colon = name.find(':');
    if (colon != std::string::npos) name.resize(colon);
    return name;
}

static bool isValidAndroidId(const std::string& id) {
    if (id.size() != 16) return false;
    for (char c : id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

static bool readAppAndroidId(const std::string& packageName, std::string& out) {
    std::ifstream file(config_file);
    if (!file.is_open()) return false;
    try {
        json config = json::parse(file);
        if (!config.contains("APP_PROFILES") || !config["APP_PROFILES"].is_object()) return false;
        const auto it = config["APP_PROFILES"].find(packageName);
        if (it == config["APP_PROFILES"].end() || !it->is_object()) return false;
        const std::string id = it->value("ANDROID_ID", "");
        if (!isValidAndroidId(id)) return false;
        out = id;
        return true;
    } catch (...) {
        return false;
    }
}

static jobject makeGenericMethodType(JNIEnv* env, jclass methodTypeClass, jclass objectClass, int argc) {
    jmethodID methodType = env->GetStaticMethodID(
        methodTypeClass, "methodType", "(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;");
    if (!methodType) { env->ExceptionClear(); return nullptr; }
    jobjectArray params = env->NewObjectArray(argc, objectClass, nullptr);
    if (!params) return nullptr;
    jobject result = env->CallStaticObjectMethod(methodTypeClass, methodType, objectClass, params);
    env->DeleteLocalRef(params);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return result;
}

static jobject buildAndroidIdHooker(JNIEnv* env, jobject backupMethod, const std::string& androidId, int argc) {
    if (!env || !backupMethod || (argc != 2 && argc != 3)) return nullptr;

    jclass methodClass = env->FindClass("java/lang/reflect/Method");
    jclass methodHandles = env->FindClass("java/lang/invoke/MethodHandles");
    jclass lookupClass = env->FindClass("java/lang/invoke/MethodHandles$Lookup");
    jclass methodHandleClass = env->FindClass("java/lang/invoke/MethodHandle");
    jclass methodTypeClass = env->FindClass("java/lang/invoke/MethodType");
    jclass stringClass = env->FindClass("java/lang/String");
    jclass objectClass = env->FindClass("java/lang/Object");
    if (!methodClass || !methodHandles || !lookupClass || !methodHandleClass || !methodTypeClass || !stringClass || !objectClass) {
        env->ExceptionClear(); return nullptr;
    }

    // lookup().unreflect(backup) gives us a callable handle for the original method.
    jmethodID lookupId = env->GetStaticMethodID(methodHandles, "lookup", "()Ljava/lang/invoke/MethodHandles$Lookup;");
    jmethodID unreflectId = env->GetMethodID(lookupClass, "unreflect", "(Ljava/lang/reflect/Method;)Ljava/lang/invoke/MethodHandle;");
    if (!lookupId || !unreflectId) { env->ExceptionClear(); return nullptr; }
    jobject lookup = env->CallStaticObjectMethod(methodHandles, lookupId);
    jobject original = lookup ? env->CallObjectMethod(lookup, unreflectId, backupMethod) : nullptr;
    if (env->ExceptionCheck() || !original) { env->ExceptionClear(); return nullptr; }

    // Convert the original to the generic Object...(Object) shape used by the callback.
    jobject genericType = makeGenericMethodType(env, methodTypeClass, objectClass, argc);
    jmethodID asTypeId = env->GetMethodID(methodHandleClass, "asType", "(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    jobject fallback = genericType ? env->CallObjectMethod(original, asTypeId, genericType) : nullptr;
    if (env->ExceptionCheck() || !fallback) { env->ExceptionClear(); return nullptr; }

    // constant(Object.class, androidId) -> ()Object, then drop the target arguments.
    jmethodID constantId = env->GetStaticMethodID(methodHandles, "constant", "(Ljava/lang/Class;Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;");
    jmethodID dropId = env->GetStaticMethodID(methodHandles, "dropArguments", "(Ljava/lang/invoke/MethodHandle;I[Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;");
    jmethodID insertId = env->GetStaticMethodID(methodHandles, "insertArguments", "(Ljava/lang/invoke/MethodHandle;I[Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;");
    jmethodID permuteId = env->GetStaticMethodID(methodHandles, "permuteArguments", "(Ljava/lang/invoke/MethodHandle;Ljava/lang/invoke/MethodType;[I)Ljava/lang/invoke/MethodHandle;");
    jmethodID guardId = env->GetStaticMethodID(methodHandles, "guardWithTest", "(Ljava/lang/invoke/MethodHandle;Ljava/lang/invoke/MethodHandle;Ljava/lang/invoke/MethodHandle;)Ljava/lang/invoke/MethodHandle;");
    if (!constantId || !dropId || !insertId || !permuteId || !guardId) { env->ExceptionClear(); return nullptr; }

    jstring idString = env->NewStringUTF(androidId.c_str());
    jobject constant = env->CallStaticObjectMethod(methodHandles, constantId, objectClass, idString);
    if (env->ExceptionCheck() || !constant) { env->ExceptionClear(); return nullptr; }

    jobjectArray droppedTypes = env->NewObjectArray(argc, objectClass, objectClass);
    jobject target = env->CallStaticObjectMethod(methodHandles, dropId, constant, 0, droppedTypes);
    if (env->ExceptionCheck() || !target) { env->ExceptionClear(); return nullptr; }

    // String.equals(Object), bound to the literal "android_id" -> (Object)->boolean.
    jmethodID getDeclaredMethod = env->GetMethodID(methodClass, "getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    if (!getDeclaredMethod) { env->ExceptionClear(); return nullptr; }
    jobject equalsMethod = nullptr;
    {
        jobjectArray p = env->NewObjectArray(1, objectClass, nullptr);
        env->SetObjectArrayElement(p, 0, objectClass);
        jstring eqName = env->NewStringUTF("equals");
        equalsMethod = env->CallObjectMethod(stringClass, getDeclaredMethod, eqName, p);
        env->DeleteLocalRef(eqName);
        env->DeleteLocalRef(p);
    }
    if (env->ExceptionCheck() || !equalsMethod) { env->ExceptionClear(); return nullptr; }
    jobject equalsHandle = env->CallObjectMethod(lookup, unreflectId, equalsMethod);
    if (env->ExceptionCheck() || !equalsHandle) { env->ExceptionClear(); return nullptr; }

    jobjectArray boundValues = env->NewObjectArray(1, objectClass, nullptr);
    env->SetObjectArrayElement(boundValues, 0, env->NewStringUTF("android_id"));
    jobject boundTest = env->CallStaticObjectMethod(methodHandles, insertId, equalsHandle, 0, boundValues);
    if (env->ExceptionCheck() || !boundTest) { env->ExceptionClear(); return nullptr; }

    // methodType(boolean.class, Object...) is needed by permuteArguments; obtain primitive boolean.class.
    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jfieldID typeField = env->GetStaticFieldID(booleanClass, "TYPE", "Ljava/lang/Class;");
    jobject booleanType = typeField ? env->GetStaticObjectField(booleanClass, typeField) : nullptr;
    if (env->ExceptionCheck() || !booleanType) { env->ExceptionClear(); return nullptr; }
    jmethodID methodType2 = env->GetStaticMethodID(methodTypeClass, "methodType", "(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;");
    jobjectArray testParams = env->NewObjectArray(argc, objectClass, nullptr);
    jobject testFullType = env->CallStaticObjectMethod(methodTypeClass, methodType2, booleanType, testParams);
    env->DeleteLocalRef(testParams);
    if (env->ExceptionCheck() || !testFullType) { env->ExceptionClear(); return nullptr; }

    jintArray reorder = env->NewIntArray(1);
    jint one = 1;
    env->SetIntArrayRegion(reorder, 0, 1, &one);
    jobject test = env->CallStaticObjectMethod(methodHandles, permuteId, boundTest, testFullType, reorder);
    if (env->ExceptionCheck() || !test) { env->ExceptionClear(); return nullptr; }

    jobject guarded = env->CallStaticObjectMethod(methodHandles, guardId, test, target, fallback);
    if (env->ExceptionCheck() || !guarded) { env->ExceptionClear(); return nullptr; }

    env->DeleteLocalRef(methodClass); env->DeleteLocalRef(methodHandles); env->DeleteLocalRef(lookupClass);
    env->DeleteLocalRef(methodHandleClass); env->DeleteLocalRef(methodTypeClass); env->DeleteLocalRef(stringClass); env->DeleteLocalRef(objectClass);
    return guarded;
}

static bool hookSettingsMethod(JNIEnv* env, const char* name,
                               const std::vector<jclass>& params, const std::string& androidId,
                               jobject& hookerOut) {
    jclass secure = env->FindClass("android/provider/Settings$Secure");
    jclass classClass = env->FindClass("java/lang/Class");
    if (!secure || !classClass) { env->ExceptionClear(); return false; }
    jmethodID getDeclared = env->GetMethodID(classClass, "getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    jmethodID hookCallback = env->GetMethodID(env->FindClass("java/lang/invoke/MethodHandle"), "invokeWithArguments", "([Ljava/lang/Object;)Ljava/lang/Object;");
    if (!getDeclared || !hookCallback) { env->ExceptionClear(); return false; }
    jobjectArray p = env->NewObjectArray(params.size(), classClass, nullptr);
    for (jsize i = 0; i < static_cast<jsize>(params.size()); ++i) env->SetObjectArrayElement(p, i, params[i]);
    jstring jname = env->NewStringUTF(name);
    jobject method = env->CallObjectMethod(secure, getDeclared, jname, p);
    if (env->ExceptionCheck() || !method) { env->ExceptionClear(); return false; }
    jobject hooker = buildAndroidIdHooker(env, method, androidId, static_cast<int>(params.size()));
    if (!hooker) return false;
    jobject backup = lsplant::Hook(env, method, hooker, hookCallback);
    if (!backup) { LOGE("LSPlant Hook failed for Settings.Secure.%s", name); return false; }
    hookerOut = env->NewGlobalRef(hooker);
    LOGE("Android ID hook installed: Settings.Secure.%s", name);
    return hookerOut != nullptr;
}

class COPGVDModule : public zygisk::ModuleBase {
private:
    zygisk::Api* api = nullptr;
    JNIEnv* env = nullptr;

    // Value-initialized: setInt/setLong only skip a field when it is 0, so an
    // indeterminate int here would be written straight into Build.TIME.
    DeviceInfo spoof_info{};
    std::vector<jobject> android_id_hookers;

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

        env->DeleteLocalRef(buildClass);
        if (versionClass) env->DeleteLocalRef(versionClass);
    }

public:

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!env || !args) return;
        const std::string pkg = packageFromNiceName(env, args->nice_name);
        if (pkg.empty()) return;
        std::string androidId;
        if (!readAppAndroidId(pkg, androidId)) return;
        if (!copg_lsplant::initialized()) {
            if (!copg_lsplant::init(env)) return;
        }
        jclass contentResolver = env->FindClass("android/content/ContentResolver");
        jclass stringClass = env->FindClass("java/lang/String");
        jclass intClass = env->FindClass("java/lang/Integer");
        if (!contentResolver || !stringClass || !intClass) { env->ExceptionClear(); return; }
        if (!hookSettingsMethod(env, "getString", {contentResolver, stringClass}, androidId, android_id_hookers.emplace_back())) {
            android_id_hookers.pop_back();
        }
        if (!hookSettingsMethod(env, "getStringForUser", {contentResolver, stringClass, intClass}, androidId, android_id_hookers.emplace_back())) {
            android_id_hookers.pop_back();
        }
        LOGE("App Profile Android ID active for %s: %s", pkg.c_str(), androidId.c_str());
    }

    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;

        spoofDevice();

        // Initialize ART hook engine before app specialization. The hook callbacks are backed
        // entirely by java.lang.invoke.MethodHandle objects, so no module code may be unmapped.
        copg_lsplant::init(env);
        copg_device_hooks::install(env);
    }
};

REGISTER_ZYGISK_MODULE(COPGVDModule)
