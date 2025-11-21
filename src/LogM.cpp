#include "LogM.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <cstring>
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
    currentLevel(DEBUG),
    maxFileSize(5 * 1024 * 1024), // 默认 5MB
    fileStartTime(std::time(nullptr)) {
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
    // 预创建文件（可选）
    try { std::ofstream ofs(logFilePath, std::ios::app); } catch(...) {}
}

LogM::~LogM() {
    // 无持久句柄需要关闭
}

void LogM::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(logMutex);
    logFilePath = path;
    ensurePath(logFilePath);
    fileStartTime = std::time(nullptr); // 更换文件重新计时
}

const char* LogM::levelToStr(LogLevel level) {
    if (level == DEBUG) return "DEBUG";
    if (level == INFO)  return "INFO";
    if (level == WARN)  return "WARN";
    if (level == ERROR) return "ERROR";
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
    try { std::ofstream ofs(logFilePath, std::ios::app); } catch(...) {}
    fileStartTime = now_c; // 更新开始时间
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

    std::lock_guard<std::mutex> lock(logMutex);
    // 写之前检查轮转
    rotateIfNeeded(now_c);

    std::ofstream logFile(logFilePath, std::ios::app);
    if (logFile) {
        logFile << outLine << std::endl;
    } else {
        std::fprintf(stderr, "%s\n", outLine.c_str());
    }
}