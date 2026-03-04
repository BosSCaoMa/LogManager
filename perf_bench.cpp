#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include "LogM.h"

using Clock = std::chrono::steady_clock;

#ifndef LOGM_PROJECT_DIR
#define LOGM_PROJECT_DIR "."
#endif

struct BenchResult {
    int threads;
    int perThread;
    size_t queueCapacity;
    double produceSec;
    double totalSec;
    long long linesWritten;
    uint64_t accepted;
    uint64_t dropped;
};

long long countLines(const std::string& path) {
    std::ifstream in(path);
    long long n = 0;
    std::string s;
    while (std::getline(in, s)) ++n;
    return n;
}

BenchResult runBench(int threads, int perThread, size_t queueCap) {
    const char* envLogDir = std::getenv("LOGM_LOG_DIR");
    const std::filesystem::path logDir = (envLogDir != nullptr && envLogDir[0] != '\0')
        ? std::filesystem::path(envLogDir)
        : std::filesystem::path(LOGM_PROJECT_DIR) / "log";
    const std::string logPath = (logDir / "perf_app.log").string();
    std::filesystem::create_directories(logDir);
    std::filesystem::remove(logPath);

    LogConfig cfg;
    cfg.level = LOGM_DEBUG;
    cfg.filePath = logPath;
    cfg.enableConsole = false;
    cfg.queueCapacity = queueCap;
    cfg.maxFileSize = 5ull * 1024ull * 1024ull;
    cfg.dropPolicy = DropPolicy::DROP_CURRENT;
    LogM::getInstance().init(cfg);

    auto t0 = Clock::now();
    std::vector<std::thread> workers;
    workers.reserve(threads);

    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([i, perThread]() {
            for (int j = 0; j < perThread; ++j) {
                LOG_INFO("bench thread=%d seq=%d payload=%s", i, j, "abcdefghijklmnopqrstuvwxyz0123456789");
            }
        });
    }

    for (auto& t : workers) t.join();
    auto t1 = Clock::now();

    LogM::getInstance().shutdown();
    uint64_t accepted = LogM::getInstance().getAcceptedCount();
    uint64_t dropped = LogM::getInstance().getDroppedCount();
    auto t2 = Clock::now();

    return BenchResult{
        threads,
        perThread,
        queueCap,
        std::chrono::duration<double>(t1 - t0).count(),
        std::chrono::duration<double>(t2 - t0).count(),
        countLines(logPath),
        accepted,
        dropped
    };
}

int main() {
    std::vector<BenchResult> results;
    results.push_back(runBench(1, 200000, 1 << 18));
    results.push_back(runBench(4, 200000, 1 << 19));
    results.push_back(runBench(8, 150000, 1 << 20));

    std::cout << "threads,queue_capacity,total_logs,produce_sec,total_sec,produce_tps,total_tps,accepted,dropped,drop_rate,lines_written\n";
    for (const auto& r : results) {
        long long totalLogs = 1ll * r.threads * r.perThread;
        double produceTps = totalLogs / r.produceSec;
        double totalTps = totalLogs / r.totalSec;
        double dropRate = totalLogs > 0 ? static_cast<double>(r.dropped) / static_cast<double>(totalLogs) : 0.0;
        std::cout << r.threads << ','
                  << r.queueCapacity << ','
                  << totalLogs << ','
                  << r.produceSec << ','
                  << r.totalSec << ','
                  << produceTps << ','
                  << totalTps << ','
                  << r.accepted << ','
                  << r.dropped << ','
                  << dropRate << ','
                  << r.linesWritten << '\n';
    }
    return 0;
}
