# LogManager

轻量级 C++ 日志库，支持：

- 异步写入：前台入队，后台专用线程批量落盘。
- 有界 MPSC 环形队列与丢弃策略：满时丢当前或丢最旧。
- 文件大小轮转（后台执行），自动建目录。
- 微秒时间戳与 thread_local 秒级缓存。
- 可选控制台 + 文件多 sink。

## 模块设计

- LogM 单例：管理配置、后台写线程、文件句柄与轮转状态。
- 前台入口：`LOG_xxx` 宏封装 `log()`，仅负责格式化和入队。
- 队列：有界 MPSC 环形缓冲（容量 2 的幂，原子 head/tail + 每槽 ready 标记）。
- 写线程：批量取队列数据，执行轮转、写文件、可选输出到控制台并 flush。
- 配置：`LogConfig` 统一设置级别、文件路径、队列容量、丢弃策略、是否开启控制台 sink、最大文件大小。
- 生命周期：支持 `shutdown()` 优雅停机，先 draining 再退出写线程。

## 关键技术点

- 时间格式优化：微秒级时间戳，thread_local 缓存秒级字符串，减少 `localtime` 调用。
- 异步落盘：前台无文件 IO，仅入队；后台批量写，降低锁竞争与系统调用频率。
- 有界 MPSC 队列：无锁/低锁入队，丢弃策略可选，避免无限膨胀；满时即时决策。
- 后台轮转：在写线程内检查大小并轮转，保持单一线程持有文件句柄，避免频繁打开/关闭。
- 多 sink：文件为主，控制台可选，便于调试与查看实时输出。

## 代码结构与执行路径（结合源码）

1) 入口宏（`LogM.h`）：`LOG_DEBUG/INFO/WARN/ERROR` → `LOG_BASE` → 调用 `LogM::log(...)`，先做级别检查、格式化到栈缓冲，防截断。
2) 前台日志函数（`LogM::log`，`LogM.cpp`）：
   - 获取微秒时间戳；thread_local 缓存秒级格式串；附带 `level`/`tid`/源码位置信息组装行。
   - 调用 `enqueue`：无锁/低锁 MPSC 环形缓冲（`head/tail` 原子 + `capacityMask`），满时按 `DropPolicy` 丢当前或丢最旧，成功则 `notify_one` 唤醒写线程。
3) 写线程（`writerLoop`）：
   - `queueCv` + `waitMutex` 仅用于等待唤醒；批量最多取 256 条，移动出 ring，并推进 `head`。
   - 在持 `cfgMutex` 状态下执行 `rotateIfNeeded`（文件大小轮转重命名）与写文件，按需输出控制台，最后 flush。
   - `stopFlag` 置位后继续 drain 队列，直到消费完毕再退出。
4) 配置与生命周期：
   - `init(LogConfig)`: 设定级别/路径/轮转阈值/队列容量（向上取 2 的幂）/丢弃策略/sink；重建 ring，重开文件，启动写线程（若未启动）。
   - `shutdown()`: 置 `stopFlag`，唤醒写线程，等待 join，关闭文件；析构中自动调用，亦可显式调用以确保优雅停机。
5) 轮转（`rotateIfNeeded`）：在写线程持锁时检查文件大小，重命名为 `stem_start_end.ext`，再重开文件并重置起始时间。
6) 辅助：`ensurePath` 自动建目录；`nextPow2` 计算环形容量；默认路径 Windows 为可执行同目录 `log/app.log`，其他平台为 `./log/app.log`。

## 数据结构与并发模型

- 队列：容量为 2 的幂，`tail`（生产位置）/`head`（消费位置）均为原子；`Slot{atomic<bool> ready; std::string data;}`。
- 线程模型：多生产者（调用日志的线程）+ 单消费者（写线程）。等待条件仅用于睡眠/唤醒，不保护数据；数据同步依赖原子语义。
- 丢弃策略：`DROP_CURRENT` 直接丢当前；`DROP_OLDEST` 通过 CAS 推进 `head` 丢最旧，然后重试入队。
- 批处理：写线程每次最多搬运 256 条，减少持锁写文件次数和 flush 次数。
- 锁粒度：前台入队不持互斥；写线程在写入/轮转阶段持 `cfgMutex` 保护文件句柄与轮转状态。

## 流程细节（关键路径）

- 入队（`enqueue`）
  - 读取 `tail`/`head` 判断是否满；满则按策略处理。
  - CAS 递增 `tail` 预留槽位 → 写入 `data` → `ready=true` → `notify_one`。
- 消费（`writerLoop`）
  - wait 直到 `head < tail` 或 `stopFlag`。
  - 读取 `head`/`tail`，按索引取槽位；仅在 `ready` 为真时消费，消费后 `ready=false`，`head++`。
  - 持 `cfgMutex`：`rotateIfNeeded(now)` → 写文件 → 可选写 stdout → flush。
  - `stopFlag` 置位后继续消费剩余元素，队列清空后退出。
- 轮转（`rotateIfNeeded`）
  - 仅由写线程调用；检查当前文件大小，超阈值则以 `开始_结束` 时间戳重命名，重开新文件，重置起始时间。

## 配置项速览（`LogConfig`）

- `level`：日志最低输出级别。
- `filePath`：日志文件路径（显式配置时最高优先级）。
- `maxFileSize`：字节数，超出触发大小轮转（0 表示关闭）。
- `queueCapacity`：队列容量（向上取整到 2 的幂）。
- `dropPolicy`：队列满时丢当前或丢最旧。
- `enableConsole`：是否同时输出到控制台。

## 日志路径优先级

当 `LogConfig::filePath` 为空时，默认日志路径按以下优先级解析：

1. 环境变量 `LOGM_LOG_DIR`（目录）→ `${LOGM_LOG_DIR}/app.log`
2. 编译期项目根目录（`LOGM_PROJECT_DIR`，本仓库 CMake 自动注入）→ `${PROJECT_ROOT}/log/app.log`
3. 运行时可执行文件目录推断出的根目录（例如 `build/`、`bin/` 的上一层）→ `${root}/log/app.log`
4. 当前工作目录回退 → `./log/app.log`

说明：作为动态库被其他项目加载时，如果未显式传 `filePath`，可通过设置 `LOGM_LOG_DIR` 强制落到目标项目根目录。

## 压测数据（可量化）

测试日期：2026-03-04（Linux）

执行命令：

```bash
/home/ubuntu/code/LogManager/build/logm_perf_bench > /home/ubuntu/code/LogManager/build/log/perf_latest.csv
```

测试配置（`perf_bench.cpp`）：

- `maxFileSize = 5MB`（启用轮转）
- `enableConsole = false`
- `dropPolicy = DROP_CURRENT`
- 队列容量分别为 `1<<18`、`1<<19`、`1<<20`

原始结果（最新一轮）：

| threads | queue_capacity | total_logs | produce_sec | total_sec | produce_tps | total_tps | accepted | dropped | drop_rate | lines_written |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 262144 | 200000 | 0.398072 | 0.398184 | 502421 | 502281 | 200000 | 0 | 0 | 6110 |
| 4 | 524288 | 800000 | 0.589171 | 0.734858 | 1.35784e+06 | 1.08865e+06 | 800000 | 0 | 0 | 21605 |
| 8 | 1048576 | 1200000 | 0.795778 | 1.18033 | 1.50796e+06 | 1.01667e+06 | 1200000 | 0 | 0 | 2796 |

可用于面试描述的结论：

- 在 8 线程场景，前台生产峰值约 `1.51M logs/s`，端到端约 `1.02M logs/s`。
- 通过增大队列容量 + 异步批量写，三组压测 `drop_rate` 均为 `0`（本轮数据）。
- 当前启用 5MB 轮转，`lines_written` 仅统计当前活跃文件，不等于总落盘行数；完整写入以 `accepted` 字段为准。
- 指标定义：`produce_tps = total_logs / produce_sec`，`total_tps = total_logs / total_sec`。

## 已知限制

- 入队仍有少量 CAS 重试开销；无信号/崩溃钩子，异常宕机时队列中数据可能丢失。
- `writerLoop` 已支持按“行数阈值 + 时间间隔”flush，但轮转失败仍仅 stderr，无重试/告警；日志为纯文本，未提供结构化输出。



## 问答

- 并发模型与队列选择
  - 问：为什么用 MPSC 环形队列？有界策略怎么做？
  - 答：多生产者单消费者写线程，队列容量取 2 的幂，用原子 head/tail+ready 标记；满时按策略丢当前或丢最旧，避免内存膨胀和阻塞。
- 异步可靠性与丢数据风险
  - 问：崩溃/断电会不会丢日志？
  - 答：会，队列中未刷盘的会丢；目前无信号/崩溃钩子，改进方向是崩溃时尝试同步 flush 或持久化队列水位、增加丢弃计数与告警。
- flush 与性能取舍
  - 问：怎么控制 flush 频率？
  - 答：当前按“批量写 + 行数阈值（1024）+ 时间间隔（100ms）”触发 flush；可继续调参（时间/条数/字节）平衡一致性与吞吐。
- 轮转策略
  - 问：按什么轮转？失败怎么办？
  - 答：按大小轮转，重命名加开始/结束时间；失败时 stderr 提示，目前无重试/告警，改进可加重试/回退策略与按时间轮转。
- 格式化与开销
  - 问：格式化是否阻塞？
  - 答：前台只做格式化和入队，无文件 IO；使用 thread_local 缓存秒级时间戳，微秒精度；仍有 [ostringstream](vscode-file://vscode-app/d:/Users/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html)/hash tid 开销，可优化为固定缓冲 `snprintf`。
- 多 sink / 配置
  - 问：输出能否多路？
  - 答：文件为主，可选控制台；未来可扩展 JSON/结构化与更多 sink。配置项集中在 [LogConfig](vscode-file://vscode-app/d:/Users/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html)：级别、路径、最大文件、队列容量、丢弃策略、控制台开关。
- 生命周期与优雅停机
  - 问：如何确保退出时不丢日志？
  - 答：提供 [shutdown()](vscode-file://vscode-app/d:/Users/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html)，置停标志后唤醒写线程，drain 队列再退出并关文件；析构也会调用，建议显式调用。
- 线程安全与锁竞争
  - 问：还有锁吗？性能瓶颈在哪？
  - 答：入队为原子 CAS，无互斥；写线程在写/轮转阶段持一个互斥保护文件句柄。瓶颈在 flush 频率、文件 IO 和队列 CAS 失败重试。
- 可观测性
  - 问：如何知道丢了多少？
  - 答：当前已提供 `accepted/dropped/drop_rate` 指标（见压测输出）；可进一步接入监控系统或回调告警。