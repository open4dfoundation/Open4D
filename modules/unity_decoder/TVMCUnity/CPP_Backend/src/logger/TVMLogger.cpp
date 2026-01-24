// TVMLogger.cpp
#include "TVMLogger.h"
#include <iostream>
#include <mutex>
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace TVMLogger {

// --- Unity callback integration ---
typedef void (*UnityLogCallback)(const char*);
static UnityLogCallback unityCallback = nullptr;

namespace {
std::mutex& GetLoggerMutex() {
    static std::mutex m;
    return m;
}

std::function<void(LogLevel, const std::string&)>& GetCurrentLogger() {
    static std::function<void(LogLevel, const std::string&)> f = nullptr;
    return f;
}

bool& GetLoggingEnabled() {
    static bool enabled = true;
    return enabled;
}
} // anonymous namespace

// Register Unity callback
void RegisterUnityCallback(UnityLogCallback callback) {
    std::lock_guard<std::mutex> lock(GetLoggerMutex());
    unityCallback = callback;
}

//Logging interface
void SetLogger(std::function<void(LogLevel, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(GetLoggerMutex());
    GetCurrentLogger() = std::move(callback);
}

void EnableLogging(bool enabled) {
    std::lock_guard<std::mutex> lock(GetLoggerMutex());
    GetLoggingEnabled() = enabled;
}

bool IsLoggingEnabled() {
    std::lock_guard<std::mutex> lock(GetLoggerMutex());
    return GetLoggingEnabled();
}

void Log(LogLevel level, const std::string& msg) {
    std::function<void(LogLevel, const std::string&)> loggerCopy;
    bool loggingEnabled = false;
    UnityLogCallback unityCopy = nullptr;
    {
        std::lock_guard<std::mutex> lock(GetLoggerMutex());
        loggerCopy = GetCurrentLogger();
        loggingEnabled = GetLoggingEnabled();
        unityCopy = unityCallback;
    }

    // Suppress non-error logs if disabled
    if (!loggingEnabled && level != LogLevel::Error)
        return;

    // Avoid logging during stack unwinding (optional, C++17+)
#if __cplusplus >= 201703L
    if (std::uncaught_exceptions() > 0) return;
#endif

    const char* prefix = "[INFO] ";
    if (level == LogLevel::Warning) prefix = "[WARN] ";
    else if (level == LogLevel::Error) prefix = "[ERROR] ";

    const std::string fullMsg = std::string(prefix) + "TVMDecoder: " + msg;

    // Send to Unity if callback is registered (works in Editor and builds)
    if (unityCopy) {
        try {
            unityCopy(fullMsg.c_str());
        } catch (...) {
            // Silently ignore Unity callback errors
        }
    }

    // Use custom logger if set
    if (loggerCopy) {
        try {
            loggerCopy(level, fullMsg);
        } catch (const std::exception& e) {
            std::cerr << "[TVMLogger] Logger callback threw exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[TVMLogger] Logger callback threw unknown exception." << std::endl;
        }
        return;
    }

    // Fallback to platform-specific logging
#ifdef __ANDROID__
    // Android logcat output
    int priority = ANDROID_LOG_INFO;
    if (level == LogLevel::Warning) priority = ANDROID_LOG_WARN;
    else if (level == LogLevel::Error) priority = ANDROID_LOG_ERROR;
    __android_log_print(priority, "TVMDecoder", "%s", fullMsg.c_str());
#else
    // Standard console output (for debugging in Unity Editor on desktop)
    std::ostream& stream = (level == LogLevel::Error) ? std::cerr : std::cout;
    stream << fullMsg << std::endl;
#endif
}

} // namespace TVMLogger

// Export Unity callback registration for C interface
extern "C" {
void RegisterUnityLogCallback(void (*callback)(const char*)) {
    TVMLogger::RegisterUnityCallback(callback);
}
}
