#include "diagnostics.hpp"
#include <android/log.h>

#define LOG_TAG "COPG-VD"

void logConfigDiagnostics(const ConfigDiagnostics& d) {
    if (!d.error.empty()) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "config: %s", d.error.c_str());
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
        "config status: opened=%d parsed=%d section=%d build_fields=%d version_fields=%d unsupported=%d",
        d.file_opened, d.json_parsed, d.section_found, d.configured_build_fields,
        d.configured_version_fields, d.unsupported_fields);
}

void logRuntimeResult(JNIEnv*, bool buildClassFound, bool versionClassFound, const DeviceInfo& info) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
        "runtime status: Build=%s Build.VERSION=%s model=%s fingerprint=%s version_sdk=%d",
        buildClassFound ? "ready" : "missing", versionClassFound ? "ready" : "missing",
        info.model.empty() ? "<unchanged>" : "configured",
        info.fingerprint.empty() ? "<unchanged>" : "configured",
        info.version_sdk_int);
}
