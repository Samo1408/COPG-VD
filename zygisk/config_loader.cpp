#include "config_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

static std::string trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

static RomVersion readRomVersion(const std::string& path) {
    RomVersion rom;
    std::ifstream file(path);
    if (!file.is_open()) return rom;

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
            try { rom.sdk = std::stoi(sdk); } catch (...) { rom.sdk = 0; }
        }
    }
    return rom;
}

static VersionPolicy readVersionPolicy(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return VersionPolicy::Never;
    std::string value;
    std::getline(file, value);
    value = trim(value);
    if (value == "force") return VersionPolicy::Force;
    if (value == "rom") return VersionPolicy::Rom;
    return VersionPolicy::Never;
}

static std::string releaseOrCodename(const std::string& codename, const std::string& release) {
    return (codename.empty() || codename == "REL") ? release : codename;
}

static bool allowedVersionField(const RomVersion& rom, VersionPolicy policy,
                                const char* field, const std::string& value) {
    if (policy == VersionPolicy::Force) return true;
    if (policy == VersionPolicy::Never) return false;

    const std::string f(field);
    if (f == "SDK_INT" || f == "SDK_FULL") {
        if (rom.sdk == 0) return false;
        try { return std::stoi(value) <= rom.sdk; } catch (...) { return false; }
    }
    if (f == "ANDROID_VERSION") return !rom.release.empty() && value == rom.release;
    if (f == "CODENAME") return !rom.codename.empty() && value == rom.codename;
    return false;
}

bool loadDeviceInfo(const std::string& configPath,
                    const std::string& romPropPath,
                    const std::string& versionPolicyPath,
                    DeviceInfo& out,
                    ConfigDiagnostics& diagnostics) {
    out = DeviceInfo{};
    diagnostics = ConfigDiagnostics{};

    std::ifstream file(configPath);
    if (!file.is_open()) {
        diagnostics.error = "failed to open config";
        return false;
    }
    diagnostics.file_opened = true;

    try {
        json config = json::parse(file);
        diagnostics.json_parsed = true;
        if (!config.contains("COPG-VD") || !config["COPG-VD"].is_object()) {
            diagnostics.error = "COPG-VD object is missing";
            return false;
        }
        diagnostics.section_found = true;
        const auto& device = config["COPG-VD"];

        auto read = [&device](const char* key, std::string& target) {
            target = device.value(key, "");
        };
        read("BRAND", out.brand); read("DEVICE", out.device); read("MANUFACTURER", out.manufacturer);
        read("MODEL", out.model); read("FINGERPRINT", out.fingerprint); read("PRODUCT", out.product);
        read("BOARD", out.board); read("BOOTLOADER", out.bootloader); read("HARDWARE", out.hardware);
        read("ID", out.id); read("DISPLAY", out.display); read("HOST", out.host); read("USER", out.user);
        out.odm_sku = device.value("ODM_SKU", out.product);
        out.sku = device.value("SKU", out.hardware);
        read("INCREMENTAL", out.version_incremental);
        read("SECURITY_PATCH", out.version_security_patch);

        const char* buildFields[] = {"BRAND","DEVICE","MANUFACTURER","MODEL","FINGERPRINT","PRODUCT","BOARD","BOOTLOADER","HARDWARE","ID","DISPLAY","HOST","USER","INCREMENTAL","SECURITY_PATCH"};
        for (const char* key : buildFields) if (device.contains(key) && device[key].is_string() && !trim(device[key].get<std::string>()).empty()) ++diagnostics.configured_build_fields;

        if (device.contains("TIMESTAMP")) {
            const auto& value = device["TIMESTAMP"];
            if (value.is_string()) out.time = std::stoll(value.get<std::string>()) * 1000;
            else if (value.is_number_integer()) out.time = value.get<int64_t>() * 1000;
        }

        const RomVersion rom = readRomVersion(romPropPath);
        const VersionPolicy policy = readVersionPolicy(versionPolicyPath);
        const std::string codename = device.value("CODENAME", "");
        if (!trim(codename).empty() && allowedVersionField(rom, policy, "CODENAME", codename)) out.version_codename = codename;

        if (device.contains("ANDROID_VERSION")) {
            const std::string value = device["ANDROID_VERSION"].get<std::string>();
            if (allowedVersionField(rom, policy, "ANDROID_VERSION", value)) out.android_version = value;
        }
        if (device.contains("SDK_INT")) {
            const std::string value = device["SDK_INT"].get<std::string>();
            if (allowedVersionField(rom, policy, "SDK_INT", value)) {
                out.version_sdk_int = std::stoi(value);
                out.version_sdk = std::to_string(out.version_sdk_int);
            }
        }
        if (device.contains("SDK_FULL")) {
            const std::string value = device["SDK_FULL"].get<std::string>();
            if (allowedVersionField(rom, policy, "SDK_FULL", value)) {
                auto dot = value.find('.');
                int major = std::stoi(dot == std::string::npos ? value : value.substr(0, dot));
                int minor = dot == std::string::npos ? 0 : std::stoi(value.substr(dot + 1));
                out.version_sdk_int_full = major * 100000 + minor;
            }
        }
        if (!out.version_sdk_int_full && out.version_sdk_int) out.version_sdk_int_full = out.version_sdk_int * 100000;
        if (!out.version_codename.empty() || !out.android_version.empty()) {
            const std::string cod = out.version_codename.empty() ? rom.codename : out.version_codename;
            const std::string rel = out.android_version.empty() ? rom.release : out.android_version;
            out.version_release_or_codename = releaseOrCodename(cod, rel);
            out.version_release_or_preview_display = out.version_release_or_codename;
        }

        const char* versionFields[] = {"CODENAME","ANDROID_VERSION","SDK_INT","SDK_FULL"};
        for (const char* key : versionFields) if (device.contains(key)) ++diagnostics.configured_version_fields;

        // These fields are intentionally reported as unsupported by this native layer.
        const char* unsupported[] = {"PREVIEW_SDK","SDK_FINGERPRINT","UUID","ramGb","ramMb","memKb","USER_AGENT","ANDROID_ID","GSF_ID","APP_SET_ID","SIM_SERIAL"};
        for (const char* key : unsupported) if (device.contains(key)) ++diagnostics.unsupported_fields;
        return true;
    } catch (const std::exception& e) {
        diagnostics.error = e.what();
        return false;
    }
}
