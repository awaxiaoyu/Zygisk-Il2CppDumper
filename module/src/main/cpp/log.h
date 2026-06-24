//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_LOG_H
#define ZYGISK_IL2CPPDUMPER_LOG_H

#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_TAG "Perfare"

namespace zygisk_il2cppdumper_log {

inline std::mutex g_log_mutex;
inline FILE *g_log_file = nullptr;
inline std::string g_log_path;

inline bool EnsureDir(const std::string &path) {
    if (path.empty()) {
        return false;
    }

    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }

    if (mkdir(path.c_str(), 0775) == 0) {
        return true;
    }

    if (errno == EEXIST) {
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Unable to create log dir %s: %s",
                        path.c_str(), strerror(errno));
    return false;
}

inline const char *LogLevelName(int priority) {
    switch (priority) {
        case ANDROID_LOG_DEBUG:
            return "D";
        case ANDROID_LOG_WARN:
            return "W";
        case ANDROID_LOG_ERROR:
            return "E";
        case ANDROID_LOG_INFO:
        default:
            return "I";
    }
}

inline bool LogInit(const char *package_name) {
    if (!package_name || package_name[0] == '\0') {
        return false;
    }

    // Game update note: if the target package name changes, this public log
    // directory must stay in sync with GamePackageName.
    auto dir = std::string("/sdcard/").append(package_name);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "init file log package=%s dir=%s",
                        package_name, dir.c_str());
    if (!EnsureDir("/sdcard") || !EnsureDir(dir)) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "file log dir unavailable: %s",
                            dir.c_str());
        return false;
    }
    auto path = dir.append("/zygisk_il2cppdumper.log");
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = nullptr;
    }

    g_log_path = path;
    g_log_file = fopen(g_log_path.c_str(), "a");
    if (!g_log_file) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Unable to open file log: %s: %s",
                            g_log_path.c_str(), strerror(errno));
        return false;
    }

    setvbuf(g_log_file, nullptr, _IOLBF, 0);
    fprintf(g_log_file, "\n=== zygisk-il2cppdumper start pid=%d ===\n", getpid());
    fflush(g_log_file);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "file log: %s", g_log_path.c_str());
    return true;
}

inline void LogWrite(int priority, const char *fmt, ...) {
    char message[2048];

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    __android_log_write(priority, LOG_TAG, message);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) {
        fprintf(g_log_file, "[%s] %s\n", LogLevelName(priority), message);
        fflush(g_log_file);
    }
}

} // namespace zygisk_il2cppdumper_log

inline bool LogInit(const char *package_name) {
    return zygisk_il2cppdumper_log::LogInit(package_name);
}

#define LOGD(...) zygisk_il2cppdumper_log::LogWrite(ANDROID_LOG_DEBUG, __VA_ARGS__)
#define LOGW(...) zygisk_il2cppdumper_log::LogWrite(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGE(...) zygisk_il2cppdumper_log::LogWrite(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGI(...) zygisk_il2cppdumper_log::LogWrite(ANDROID_LOG_INFO, __VA_ARGS__)

#endif //ZYGISK_IL2CPPDUMPER_LOG_H
