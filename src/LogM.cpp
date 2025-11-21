#include "LogM.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <cstring>

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
    logFilePath("../log/app.log") {
    ensurePath(logFilePath);
    // 预创建文件（可选）
    try { std::ofstream ofs(logFilePath, std::ios::app); } catch(...) {}
}

LogM::~LogM() {
    // 这里没有持久打开文件句柄，无需特别处理
}

void LogM::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(logMutex);
    logFilePath = path;
    ensurePath(logFilePath);
}

const char* LogM::levelToStr(LogLevel level) {
    switch(level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default:    return "UNKNOWN";
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

    // 时间戳
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
#if defined(_MSC_VER)
    struct tm tmBuf; localtime_s(&tmBuf, &now_c); struct tm* ptm = &tmBuf;
#else
    struct tm* ptm = std::localtime(&now_c);
#endif

    std::ostringstream oss;
    oss << '[' << std::put_time(ptm, "%Y-%m-%d %H:%M:%S") << ']'
        << '[' << levelToStr(level) << ']'
        << "[tid:" << std::this_thread::get_id() << ']'
        << '[' << file << ':' << line << ' ' << func << "] "
        << message;

    std::string outLine = oss.str();

    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream logFile(logFilePath, std::ios::app);
    if (logFile) {
        logFile << outLine << std::endl;
    } else {
        std::fprintf(stderr, "%s\n", outLine.c_str());
    }
}