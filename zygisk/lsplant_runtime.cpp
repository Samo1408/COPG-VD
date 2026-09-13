#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <lsplant.hpp>
#include <dobby.h>
#include <string_view>
#include <string>

#define LOG_TAG "COPG-VD/LSPlant"
#define LLOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LLOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace copg_lsplant {

static void *g_art = nullptr;
static bool g_initialized = false;

static void *resolveArt(std::string_view name) {
    if (!g_art) {
        g_art = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
        if (!g_art) g_art = dlopen("libart.so", RTLD_NOW);
    }
    if (!g_art) return nullptr;
    std::string symbol(name);
    return dlsym(g_art, symbol.c_str());
}

jobject callbackMethod(JNIEnv *env) {
    static jobject g_callback = nullptr;
    if (g_callback) return g_callback;
    if (!env) return nullptr;
    jclass methodHandle = env->FindClass("java/lang/invoke/MethodHandle");
    jclass classClass = env->FindClass("java/lang/Class");
    jclass objectArrayClass = env->FindClass("[Ljava/lang/Object;");
    if (!methodHandle || !classClass || !objectArrayClass) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID getDeclared = env->GetMethodID(
        classClass, "getDeclaredMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    if (!getDeclared) {
        env->ExceptionClear();
        return nullptr;
    }
    jobjectArray params = env->NewObjectArray(1, classClass, nullptr);
    env->SetObjectArrayElement(params, 0, objectArrayClass);
    jstring name = env->NewStringUTF("invokeWithArguments");
    jobject method = env->CallObjectMethod(methodHandle, getDeclared, name, params);
    env->DeleteLocalRef(params);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(methodHandle);
    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(objectArrayClass);
    if (env->ExceptionCheck() || !method) {
        env->ExceptionClear();
        return nullptr;
    }
    g_callback = env->NewGlobalRef(method);
    env->DeleteLocalRef(method);
    return g_callback;
}

bool init(JNIEnv *env) {
    if (g_initialized) return true;
    if (!env) return false;

    lsplant::InitInfo info{
        .inline_hooker = [](void *target, void *replacement) -> void * {
            void *backup = nullptr;
            if (DobbyHook(target, replacement, &backup) != 0) return nullptr;
            return backup;
        },
        .inline_unhooker = [](void *target) -> bool {
            return DobbyDestroy(target) == 0;
        },
        .art_symbol_resolver = [](std::string_view symbol) -> void * {
            return resolveArt(symbol);
        },
        .art_symbol_prefix_resolver = nullptr,
        .generated_class_name = "COPGVD_LSPHooker_",
        .generated_source_name = "COPG-VD",
        .generated_field_name = "hooker",
        .generated_method_name = "{target}",
        .executable_memory_allocator = nullptr,
        .executable_memory_recycler = nullptr,
    };

    g_initialized = lsplant::Init(env, info);
    if (g_initialized) LLOGI("LSPlant initialized");
    else LLOGE("LSPlant initialization failed");
    return g_initialized;
}

void shutdown() {
    if (g_art) {
        dlclose(g_art);
        g_art = nullptr;
    }
    g_initialized = false;
}

bool initialized() { return g_initialized; }

} // namespace copg_lsplant
