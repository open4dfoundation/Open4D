#pragma once
#include <string>
#include <functional>
#include <sstream>

namespace TVMLogger {
/**
 * @brief Enum representing logging severity levels.
 */
enum class LogLevel {
    Info,    /// Informational messages
    Warning, /// Warning messages
    Error    ///<Error messages
};

/**
 * @brief Unity callback type for log messages
 */
typedef void (*UnityLogCallback)(const char*);

/**
 * @brief RegisterUnityCallback: Register a Unity logging callback
 * @param callback Function pointer to Unity's log handler
 */
void RegisterUnityCallback(UnityLogCallback callback);

/**
 * @brief SetLogger: Set a custom logging callback.
 * @param callback A function that takes a LogLevel and message string.
 */
void SetLogger(std::function<void(LogLevel, const std::string&)> callback);

/**
 * @brief EnableLogging: Enable or disable logging globally.
 * @param enabled True to enable logging, false to disable.
 */
void EnableLogging(bool enabled);

/**
 * @brief IsLoggingEnabled: Check whether logging is currently enabled.
 * @return True if logging is enabled, false otherwise.
 */
bool IsLoggingEnabled();

/**
 * @brief Log: Core logger function (thread-safe and static-safe).
 * @param level: LogLevel representing the severity level.
 * @param msg: A string representing the message to log.
 */
void Log(LogLevel level, const std::string& msg);

// --- Template helpers for formatted logging ---
/**
 * @brief AppendToStream: Append a single argument to a stringstream.
 * @tparam T: Type of the argument.
 * @param ss: The stringstream to append to.
 * @param arg: The value to append.
 */
template <typename T>
void AppendToStream(std::ostringstream& ss, const T& arg) {
    ss << arg;
}

/**
 * @brief AppendToStream: Recursively append multiple arguments to a stringstream.
 * @tparam T: Type of the first argument.
 * @tparam Args: Types of the remaining arguments.
 * @param ss: The stringstream to append to.
 * @param first: The first value to append.
 * @param rest: Remaining values to append.
 */
template <typename T, typename... Args>
void AppendToStream(std::ostringstream& ss, const T& first, const Args&... rest) {
    ss << first;
    AppendToStream(ss, rest...);
}

/**
 * @brief Log an informational message with variadic arguments.
 * @tparam Args Types of the arguments.
 * @param args The values to be concatenated and logged.
 */
template <typename... Args>
void LogInfo(const Args&... args) {
    if (!IsLoggingEnabled()) return;
    std::ostringstream ss;
    AppendToStream(ss, args...);
    Log(LogLevel::Info, ss.str());
}

/**
 * @brief Log a warning message with variadic arguments.
 * @tparam Args Types of the arguments.
 * @param args The values to be concatenated and logged.
 */
template <typename... Args>
void LogWarn(const Args&... args) {
    if (!IsLoggingEnabled()) return;
    std::ostringstream ss;
    AppendToStream(ss, args...);
    Log(LogLevel::Warning, ss.str());
}

/**
 * @brief LogError: Log an error message with variadic arguments.
 * @tparam Args: Types of the arguments.
 * @param args: The values to be concatenated and logged.
 */
template <typename... Args>
void LogError(const Args&... args) {
    std::ostringstream ss;
    AppendToStream(ss, args...);
    Log(LogLevel::Error, ss.str());
}

//Convenience macros
/**
 * @brief Log an informational message.
 */
#define LOG_INFO(...)  TVMLogger::LogInfo(__VA_ARGS__)

/**
 * @brief Log a warning message.
 */
#define LOG_WARN(...)  TVMLogger::LogWarn(__VA_ARGS__)

/**
 * @brief Log an error message.
 */
#define LOG_ERROR(...) TVMLogger::LogError(__VA_ARGS__)

} // namespace TVMLogger

// C interface for Unity - outside namespace
extern "C" {
/**
     * @brief Register Unity logging callback
     * @param callback Function pointer to Unity's log handler
     */
void RegisterUnityLogCallback(void (*callback)(const char*));
}
