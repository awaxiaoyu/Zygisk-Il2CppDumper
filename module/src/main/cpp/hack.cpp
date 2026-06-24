//
// Created by Perfare on 2020/7/4.
//

#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <string>

constexpr int kIl2CppWaitSeconds = 120;

static void UnmapLibraryData(void *&data, size_t &length) {
    if (data && data != MAP_FAILED && length > 0) {
        munmap(data, length);
    }
    data = nullptr;
    length = 0;
}

static void *OpenLibraryCandidates(const char *const *candidates, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const char *candidate = candidates[i];
        if (!candidate || candidate[0] == '\0') {
            continue;
        }

        void *handle = dlopen(candidate, RTLD_NOW);
        if (handle) {
            LOGI("dlopen %s success", candidate);
            return handle;
        }

        const char *error = dlerror();
        LOGW("dlopen %s failed: %s", candidate, error ? error : "unknown");
    }
    return nullptr;
}

void hack_start(const char *game_data_dir) {
    bool load = false;
    // Game update note: if a new Unity version delays libil2cpp loading longer,
    // tune kIl2CppWaitSeconds instead of removing the guarded wait.
    for (int i = 0; i < kIl2CppWaitSeconds; i++) {
        void *handle = xdl_open("libil2cpp.so", XDL_DEFAULT);
        if (!handle) {
            handle = xdl_open("libil2cpp.so", XDL_TRY_FORCE_LOAD);
        }
        if (handle) {
            load = true;
            if (il2cpp_api_init(handle)) {
                il2cpp_dump(game_data_dir);
            } else {
                LOGE("skip dump because il2cpp api init failed");
            }
            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        LOGI("libil2cpp.so not found in thread %d", gettid());
    }
}

std::string GetLibDir(JavaVM *vms) {
    if (!vms) {
        LOGE("JavaVM is null");
        return {};
    }
    JNIEnv *env = nullptr;
    if (vms->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
        LOGE("AttachCurrentThread failed");
        return {};
    }
    auto finish = [&](std::string result) {
        vms->DetachCurrentThread();
        return result;
    };

    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            if (application) {
                jclass application_clazz = env->GetObjectClass(application);
                if (application_clazz) {
                    jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                      "getApplicationInfo",
                                                                      "()Landroid/content/pm/ApplicationInfo;");
                    if (get_application_info) {
                        jobject application_info = env->CallObjectMethod(application,
                                                                         get_application_info);
                        if (application_info) {
                            jclass application_info_clazz = env->GetObjectClass(application_info);
                            if (application_info_clazz) {
                                jfieldID native_library_dir_id = env->GetFieldID(
                                        application_info_clazz, "nativeLibraryDir",
                                        "Ljava/lang/String;");
                                if (native_library_dir_id) {
                                    auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                            application_info, native_library_dir_id);
                                    if (native_library_dir_jstring) {
                                        auto path = env->GetStringUTFChars(native_library_dir_jstring,
                                                                           nullptr);
                                        if (path) {
                                            LOGI("lib dir %s", path);
                                            std::string lib_dir(path);
                                            env->ReleaseStringUTFChars(native_library_dir_jstring,
                                                                       path);
                                            return finish(lib_dir);
                                        }
                                    }
                                    LOGE("nativeLibraryDir value not found");
                                } else {
                                    LOGE("nativeLibraryDir not found");
                                }
                            } else {
                                LOGE("ApplicationInfo class not found");
                            }
                        } else {
                            LOGE("ApplicationInfo not found");
                        }
                    } else {
                        LOGE("getApplicationInfo not found");
                    }
                } else {
                    LOGE("application class not found");
                }
            } else {
                LOGE("currentApplication returned null");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
    }
    return finish({});
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;

    void *(*loadLibrary)(const char *libpath, int flag);

    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);

    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;

    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    sleep(5);

    const char *libart_candidates[] = {
            "libart.so",
            "/apex/com.android.art/lib64/libart.so",
            "/apex/com.android.art/lib/libart.so",
            "/system/lib64/libart.so",
            "/system/lib/libart.so",
    };
    auto libart = OpenLibraryCandidates(libart_candidates,
                                        sizeof(libart_candidates) / sizeof(libart_candidates[0]));
    if (!libart) {
        LOGE("dlopen libart.so failed from all candidates");
        return false;
    }
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart,
                                                                             "JNI_GetCreatedJavaVMs");
    LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);
    if (!JNI_GetCreatedJavaVMs) {
        LOGE("JNI_GetCreatedJavaVMs not found");
        return false;
    }
    JavaVM *vms_buf[1];
    JavaVM *vms = nullptr;
    jsize num_vms = 0;
    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
    if (status == JNI_OK && num_vms > 0) {
        vms = vms_buf[0];
    } else {
        LOGE("GetCreatedJavaVMs error");
        return false;
    }

    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        UnmapLibraryData(data, length);
        return false;
    }
    if (!data || length == 0) {
        LOGE("NativeBridge arm library data not available");
        return false;
    }

    const char *native_bridge_candidates[] = {
            "libhoudini.so",
            "/system/lib64/libhoudini.so",
            "/system/lib/libhoudini.so",
            "/system/lib64/libndk_translation.so",
            "/system/lib/libndk_translation.so",
    };
    auto nb = OpenLibraryCandidates(native_bridge_candidates,
                                    sizeof(native_bridge_candidates) /
                                    sizeof(native_bridge_candidates[0]));
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        const char *native_bridge_prop_candidates[] = {
                native_bridge.c_str(),
        };
        nb = OpenLibraryCandidates(native_bridge_prop_candidates,
                                   sizeof(native_bridge_prop_candidates) /
                                   sizeof(native_bridge_prop_candidates[0]));
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);

            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            if (fd == -1) {
                LOGE("memfd_create failed");
                UnmapLibraryData(data, length);
                return false;
            }
            if (ftruncate(fd, (off_t) length) != 0) {
                LOGE("ftruncate memfd failed");
                close(fd);
                UnmapLibraryData(data, length);
                return false;
            }
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            if (mem == MAP_FAILED) {
                LOGE("mmap memfd failed");
                close(fd);
                UnmapLibraryData(data, length);
                return false;
            }
            memcpy(mem, data, length);
            munmap(mem, length);
            UnmapLibraryData(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);

            void *arm_handle = nullptr;
            if (api_level >= 26 && callbacks->loadLibraryExt) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else if (callbacks->loadLibrary) {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                if (!callbacks->getTrampoline) {
                    LOGE("NativeBridgeGetTrampoline not found");
                    close(fd);
                    return false;
                }
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                if (!init) {
                    LOGE("JNI_OnLoad trampoline not found");
                    close(fd);
                    return false;
                }
                init(vms, (void *) game_data_dir);
                close(fd);
                return true;
            }
            close(fd);
        }
    }
    UnmapLibraryData(data, length);
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}

#endif
