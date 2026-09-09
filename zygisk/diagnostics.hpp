#pragma once
#include "config_loader.hpp"
#include <jni.h>

void logConfigDiagnostics(const ConfigDiagnostics& diagnostics);
void logRuntimeResult(JNIEnv* env, bool buildClassFound, bool versionClassFound, const DeviceInfo& info);
