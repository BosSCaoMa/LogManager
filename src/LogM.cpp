#include "LogM.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

// 帮助函数：保证路径存在
static void ensurePath(const std::string& filePath) {
    try {
        std::filesystem::path p(filePath);
        auto dir = p.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "[LogM] create directories failed: %s\n", ex.what());
    }
}

LogM &LogM::getInstance() {
    static LogM instance;
    return instance;
}

LogM::LogM() :
    currentLevel(LOGM_INFO),
    queueCapacity(8192),
    dropPolicy(DropPolicy::DROP_CURRENT),
    enableConsole(true),
    stopFlag(false),
    maxFileSize(5 * 1024 * 1024), // 默认 5MB
    fileStartTime(std::time(nullptr)),
    head(0),
    tail(0),
    capacityMask(0) {
#ifdef _WIN32
    char modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (len > 0) {
        std::filesystem::path exePath(modulePath);
        auto exeDir = exePath.parent_path();
        logFilePath = (exeDir / "log" / "app.log").string();
    } else {
        logFilePath = "log/app.log"; // 退化方案
    }
#else
    logFilePath = "./log/app.log";
#endif
    ensurePath(logFilePath);
    openFileUnlocked();
    startWriter();
}

LogM::~LogM() {
    shutdown();
}

void LogM::init(const LogConfig& cfg) {
    std::lock_guard<std::mutex> lk(cfgMutex);
    currentLevel.store(cfg.level, std::memory_order_relaxed);
    if (!cfg.filePath.empty()) {
        logFilePath = cfg.filePath;
    }
    maxFileSize = cfg.maxFileSize;
    queueCapacity = std::max<size_t>(64, cfg.queueCapacity);
    dropPolicy = cfg.dropPolicy;
    enableConsole = cfg.enableConsole;
    ensurePath(logFilePath);
    openFileUnlocked();
    size_t cap = std::max<size_t>(64, cfg.queueCapacity);
    queueCapacity = nextPow2(cap);
    capacityMask = queueCapacity - 1;
    ring.clear();
    ring.resize(queueCapacity);
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
    stopFlag.store(false, std::memory_order_relaxed);
    startWriter();
}

void LogM::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(cfgMutex);
    logFilePath = path;
    ensurePath(logFilePath);
    fileStartTime = std::time(nullptr); // 更换文件重新计时
    openFileUnlocked();
}

const char* LogM::levelToStr(LogLevel level) {
    if (level == LOGM_DEBUG) return "DEBUG";
    if (level == LOGM_INFO)  return "INFO";
    if (level == LOGM_WARN)  return "WARN";
    if (level == LOGM_ERROR) return "ERROR";
    return "UNKNOWN";
}

// 轮转：需持锁调用
void LogM::rotateIfNeeded(std::time_t now_c) {
    if (maxFileSize == 0) return; // 不启用
    std::error_code ec;
    auto sz = std::filesystem::file_size(logFilePath, ec);
    if (ec) return;
    if (sz < maxFileSize) return;

    // 构造新的文件名：app_开始时间_结束时间.log
    // 时间格式：YYYYMMDD-HHMMSS
    auto formatTime = [](std::time_t t){
        char buf[32];
#if defined(_MSC_VER)
        struct tm tmBuf; localtime_s(&tmBuf, &t); struct tm* ptm = &tmBuf;
#else
        struct tm* ptm = std::localtime(&t);
#endif
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", ptm);
        return std::string(buf);
    };

    std::string startStr = formatTime(fileStartTime);
    std::string endStr   = formatTime(now_c);

    std::filesystem::path p(logFilePath);
    std::string stem = p.stem().string();
    std::string ext  = p.extension().string();
    std::filesystem::path rotated = p.parent_path() / (stem + "_" + startStr + "_" + endStr + ext);

    std::error_code renEc;
    std::filesystem::rename(p, rotated, renEc);
    if (renEc) {
        std::fprintf(stderr, "[LogM] rotate rename failed: %s\n", renEc.message().c_str());
        return;
    }

    // 创建新的文件
    openFileUnlocked();
    fileStartTime = now_c; // 更新开始时间
}

void LogM::openFileUnlocked() {
    if (logFile.is_open()) {
        logFile.close();
    }
    ensurePath(logFilePath);
    try {
        logFile.open(logFilePath, std::ios::app);
    } catch(...) {}
}

size_t LogM::nextPow2(size_t v) const {
    if (v == 0) return 1;
    v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}

void LogM::startWriter() {
    if (writerThread.joinable()) return; // 只启动一次
    writerThread = std::thread(&LogM::writerLoop, this);
}

void LogM::shutdown() {
    bool expected = false;
    if (!stopFlag.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        // 已经关闭
    }
    queueCv.notify_all();
    if (writerThread.joinable()) {
        writerThread.join();
    }
    std::lock_guard<std::mutex> lk(cfgMutex);
    if (logFile.is_open()) logFile.close();
}

bool LogM::enqueue(std::string&& line) {
    while (true) {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t h = head.load(std::memory_order_acquire);
        if (t - h >= queueCapacity) {
            if (dropPolicy == DropPolicy::DROP_CURRENT) {
                return false;
            } else {
                // 丢最旧，前移 head
                size_t newH = h + 1;
                if (!head.compare_exchange_weak(h, newH, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    continue;
                }
                size_t idx = h & capacityMask;
                ring[idx].ready.store(false, std::memory_order_release);
                continue;
            }
        }
        if (tail.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            size_t idx = t & capacityMask;
            ring[idx].data = std::move(line);
            ring[idx].ready.store(true, std::memory_order_release);
            queueCv.notify_one();
            return true;
        }
    }
}

/*等 → 取 → 写 → 退出判断*/
void LogM::writerLoop() {
    std::vector<std::string> batch;
    batch.reserve(256);
    while (true) {
        { // 用条件变量睡眠，避免空转
            std::unique_lock<std::mutex> lk(waitMutex);
            /* 这里 wait 只靠 head < tail 判断“有数据”，但真正能不能取，还要看每个槽位 s.ready */
            queueCv.wait(lk, [this]{
                return stopFlag.load(std::memory_order_relaxed) ||
                    head.load(std::memory_order_acquire) < tail.load(std::memory_order_acquire);
            });
        }

        // 批量取出数据
        while (batch.size() < 256) {
            size_t h = head.load(std::memory_order_relaxed);
            size_t t = tail.load(std::memory_order_acquire);
            if (h >= t) break;
            size_t idx = h & capacityMask;
            Slot &s = ring[idx];

            /* 为什么既要 head/tail，又要 s.ready？
            tail 表示“已经预定/提交了多少条日志位置”（常见做法是生产者先拿到一个序号，写数据，再设置 ready）
            但生产者可能已经把 tail 往前推进了，slot 数据还没写完或没标记完成。
            所以消费者要检查 s.ready：只有 ready 才能安全 move 数据。 */
            if (!s.ready.load(std::memory_order_acquire)) break;
            batch.push_back(std::move(s.data)); // string的 move 语义，避免复制
            s.ready.store(false, std::memory_order_release);
            head.store(h + 1, std::memory_order_release);
        }

        if (batch.empty()) {
            if (stopFlag.load(std::memory_order_relaxed) &&
                head.load(std::memory_order_acquire) >= tail.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        // 写入阶段：加配置锁，可能 rotate，然后写文件/控制台
        std::time_t now_c = std::time(nullptr);
        std::lock_guard<std::mutex> lk(cfgMutex); // 保护配置与文件句柄（比如 logFile 的切换/滚动、enableConsole 等），避免其他线程修改配置时和写线程冲突
        rotateIfNeeded(now_c);
        for (auto &line : batch) {
            if (logFile.is_open()) {
                logFile << line << '\n';
            }
            if (enableConsole) {
                std::fputs(line.c_str(), stdout);
                std::fputc('\n', stdout);
            }
        }
        if (logFile.is_open()) {
            logFile.flush(); // 系统调用。把缓冲区里的数据立刻写到文件（或 stdout）底层，确保已写入的日志落盘或输出
        }
        batch.clear();

        if (stopFlag.load(std::memory_order_relaxed) &&
            head.load(std::memory_order_acquire) >= tail.load(std::memory_order_acquire)) {
            break;
        }
    }
}

void LogM::log(LogLevel level,
               const char *file,
               int line,
               const char *func,
               const char *message) {
    if (!enabled(level)) {
        return; // 低于当前级别，不输出
    }

    auto now = std::chrono::system_clock::now();
    auto usSinceEpoch = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    time_t now_c = std::chrono::system_clock::to_time_t(now);

    static thread_local time_t cachedSecond = 0; // 缓存的秒
    static thread_local char timeBuff[32] = {0}; // YYYY-MM-DD HH:MM:SS
    if (now_c != cachedSecond) {
#if defined(_MSC_VER)
        struct tm tmBuf; localtime_s(&tmBuf, &now_c); struct tm* ptm = &tmBuf;
#else
        struct tm* ptm = std::localtime(&now_c);
#endif
        std::strftime(timeBuff, sizeof(timeBuff), "%Y-%m-%d %H:%M:%S", ptm);
        cachedSecond = now_c;
    }
    auto micros = usSinceEpoch % std::chrono::seconds(1);
    size_t tidVal = std::hash<std::thread::id>{}(std::this_thread::get_id());

    std::ostringstream oss;
    oss << '[' << timeBuff << '.' << std::setw(6) << std::setfill('0') << micros.count() << ']'
        << '[' << levelToStr(level) << ']'
        << "[tid:" << tidVal << ']'
        << '[' << file << ':' << line << ' ' << func << "] "
        << message;

    std::string outLine = oss.str();
    enqueue(std::move(outLine));
}