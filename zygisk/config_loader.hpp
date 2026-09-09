#pragma once

#include <cstdint>
#include <string>

struct RomVersion {
    std::string release;
    std::string codename;
    int sdk = 0;
};

enum class VersionPolicy { Never, Rom, Force };

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
    std::string version_codename;
    std::string version_release_or_codename;
    std::string version_release_or_preview_display;
};

struct ConfigDiagnostics {
    bool file_opened = false;
    bool json_parsed = false;
    bool section_found = false;
    int configured_build_fields = 0;
    int configured_version_fields = 0;
    int unsupported_fields = 0;
    std::string error;
};

bool loadDeviceInfo(const std::string& configPath,
                    const std::string& romPropPath,
                    const std::string& versionPolicyPath,
                    DeviceInfo& out,
                    ConfigDiagnostics& diagnostics);
