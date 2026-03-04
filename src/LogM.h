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
#include <cstdint>
#include <cstring>
#include <fstream>
#include <condition_variable>
#include <vector>
// 日志级别
enum LogLevel {
    LOGM_DEBUG = 0,
    LOGM_INFO = 1,
    LOGM_WARN = 2,
    LOGM_ERROR = 3
};

// 队列满时的策略
enum class DropPolicy {
    DROP_CURRENT, // 丢弃当前条目
    DROP_OLDEST   // 丢弃最旧条目腾出空间
};

struct LogConfig {
    LogLevel level = LOGM_INFO;
    std::string filePath;          // 默认构造后在实现中设置
    size_t maxFileSize = 5 * 1024 * 1024; // 5MB
    size_t queueCapacity = 8192;   // 环形队列容量
    DropPolicy dropPolicy = DropPolicy::DROP_CURRENT;
    bool enableConsole = true;     // 是否同时输出到控制台
};

// 添加 LOGM_API 导出类符号
class LOGM_API LogM {
public:
    static LogM& getInstance();

    // 初始化配置（需在多线程写日志前调用一次）
    void init(const LogConfig& cfg);
    // 优雅停机（可选调用；析构会自动调用）
    void shutdown();

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

    // 统计信息
    uint64_t getAcceptedCount() const { return acceptedCount.load(std::memory_order_relaxed); }
    uint64_t getDroppedCount() const { return droppedCount.load(std::memory_order_relaxed); }

    static const char* levelToStr(LogLevel level);

    ~LogM();

private:
    LogM();
    LogM(const LogM&) = delete;
    LogM& operator=(const LogM&) = delete;

    // 轮转检查（在持锁状态下调用）
    void rotateIfNeeded(std::time_t now_c);
    void openFileUnlocked();
    void writerLoop();
    void startWriter();
    bool enqueue(std::string&& line);
    size_t nextPow2(size_t v) const;

    std::atomic<LogLevel> currentLevel; // 原子，避免竞态
    std::string logFilePath;
    std::mutex cfgMutex; // 保护文件句柄与轮转参数
    std::ofstream logFile;
    std::condition_variable queueCv;
    std::mutex waitMutex; // 仅用于条件变量等待，不保护队列数据

    struct Slot {
        std::atomic<bool> ready{false};
        std::string data;

        Slot() = default;
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;

        Slot(Slot&& other) noexcept
            : ready(other.ready.load(std::memory_order_relaxed)),
              data(std::move(other.data)) {
            other.ready.store(false, std::memory_order_relaxed);
        }

        Slot& operator=(Slot&& other) noexcept {
            if (this != &other) {
                ready.store(other.ready.load(std::memory_order_relaxed), std::memory_order_relaxed);
                data = std::move(other.data);
                other.ready.store(false, std::memory_order_relaxed);
            }
            return *this;
        }
    };
    std::vector<Slot> ring;
    size_t capacityMask; // 容量为 2^n，mask = capacity - 1
    std::atomic<size_t> head; // 消费位置
    std::atomic<size_t> tail; // 生产位置
    size_t queueCapacity;
    DropPolicy dropPolicy;
    bool enableConsole;
    std::atomic<bool> stopFlag;
    std::thread writerThread;

    size_t maxFileSize;        // 触发轮转的大小
    std::time_t fileStartTime; // 当前文件开始时间
    std::atomic<uint64_t> acceptedCount; // 入队成功计数
    std::atomic<uint64_t> droppedCount;  // 丢弃计数
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

#define LOG_DEBUG(fmt, ...) LOG_BASE(LOGM_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG_BASE(LOGM_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG_BASE(LOGM_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_BASE(LOGM_ERROR, fmt, ##__VA_ARGS__)

#endif // LOGM_H