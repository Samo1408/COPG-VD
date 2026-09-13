#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <lsplant.hpp>
#include <dobby.h>
#include <string_view>

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

bool init(JNIEnv *env) {
    if (g_initialized) return true;
    if (!env) return false;

    lsplant::InitInfo info{
        .inline_hooker = [](void *target, void *replacement) -> void * {
            void *backup = nullptr;
            if (DobbyHook(target, replacement, &backup) != RT_SUCCESS) return nullptr;
            return backup;
        },
        .inline_unhooker = [](void *target) -> bool {
            return DobbyDestroy(target) == RT_SUCCESS;
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
