#ifndef LOGM_H
#define LOGM_H

#include <string>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <thread>

// 日志级别
enum LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class LogM {
public:
    static LogM& getInstance();

    // 已格式化消息入口（线程安全）
    void log(LogLevel level,
             const char* file,
             int line,
             const char* func,
             const char* message);

    // 设置 / 获取当前输出最低级别
    void setLevel(LogLevel level) { currentLevel.store(level, std::memory_order_relaxed); }
    LogLevel getLevel() const { return currentLevel.load(std::memory_order_relaxed); }
    bool enabled(LogLevel level) const { return level >= getLevel(); }

    // 设置 / 获取日志文件路径（自动建目录）
    void setLogFile(const std::string& path);
    const std::string& getLogFile() const { return logFilePath; }

    static const char* levelToStr(LogLevel level);

    ~LogM();

private:
    LogM();
    LogM(const LogM&) = delete;
    LogM& operator=(const LogM&) = delete;

    std::atomic<LogLevel> currentLevel; // 原子，避免竞态
    std::string logFilePath;
    std::mutex logMutex; // 保护文件写
};

// -----------------------------------------------------------------------------
// 通用宏：避免重复代码；使用 enabled() 而不是访问私有成员
// -----------------------------------------------------------------------------
#define LOG_BASE(level, fmt, ...)                                                     \
    do {                                                                             \
        auto& _lg = LogM::getInstance();                                             \
        if (_lg.enabled(level)) {                                                    \
            char _buff[512];                                                         \
            int _n = std::snprintf(_buff, sizeof(_buff), fmt, ##__VA_ARGS__);        \
            if (_n < 0) {                                                            \
                std::snprintf(_buff, sizeof(_buff), "<format error>");             \
            } else if (_n >= (int)sizeof(_buff)) {                                   \
                /* 截断标记 */                                                      \
                const char* suffix = "...(truncated)";                              \
                size_t len = sizeof(_buff) - 1;                                      \
                size_t sLen = std::strlen(suffix);                                   \
                if (len > sLen) {                                                    \
                    std::memcpy(_buff + len - sLen - 1, suffix, sLen);               \
                    _buff[len] = '\0';                                              \
                }                                                                    \
            }                                                                        \
            _lg.log(level, __FILE__, __LINE__, __func__, _buff);                     \
        }                                                                            \
    } while (0)

#define LOG_DEBUG(fmt, ...) LOG_BASE(DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG_BASE(INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG_BASE(WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_BASE(ERROR, fmt, ##__VA_ARGS__)

#endif // LOGM_H