#ifdef _WIN32
    #ifdef LOGM_EXPORTS
        #define LOGM_API __declspec(dllexport)
    #else
        #define LOGM_API __declspec(dllimport)
    #endif
#else
    #define LOGM_API
#endif

#ifndef LOGM_H
#define LOGM_H

#include <string>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <thread>

// 日志级别
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

// 添加 LOGM_API 导出类符号
class LOGM_API LogM {
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

    // 设置最大文件大小（字节），超过则轮转
    void setMaxFileSize(size_t bytes) { maxFileSize = bytes; }

    static const char* levelToStr(LogLevel level);

    ~LogM();

private:
    LogM();
    LogM(const LogM&) = delete;
    LogM& operator=(const LogM&) = delete;

    // 轮转检查（在持锁状态下调用）
    void rotateIfNeeded(std::time_t now_c);

    std::atomic<LogLevel> currentLevel; // 原子，避免竞态
    std::string logFilePath;
    std::mutex logMutex; // 保护文件写

    size_t maxFileSize;          // 触发轮转的大小
    std::time_t fileStartTime;   // 当前文件开始时间
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
                const char* suffix = "...(truncated)";                              \
                size_t keep = sizeof(_buff) - std::strlen(suffix) - 1;               \
                if (keep > 0) {                                                      \
                    _buff[keep] = '\0';                                             \
                    std::strcat(_buff, suffix);                                      \
                } else {                                                             \
                    std::snprintf(_buff, sizeof(_buff), "%s", suffix);             \
                }                                                                    \
            }                                                                        \
            _lg.log(level, __FILE__, __LINE__, __func__, _buff);                     \
        }                                                                            \
    } while (0)

#define LOG_DEBUG(fmt, ...) LOG_BASE(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG_BASE(LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG_BASE(LogLevel::WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_BASE(LogLevel::ERROR, fmt, ##__VA_ARGS__)

#endif // LOGM_H