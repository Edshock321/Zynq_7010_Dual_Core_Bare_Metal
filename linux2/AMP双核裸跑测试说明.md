# ZYNQ7010 双核 Cortex-A9 AMP 裸跑验证说明（Vitis 2024.1）

工作空间：`E:\02_Proj\00_FPGA\SDK\WorkSpace\linux2`
硬件：XC7Z010（PS 双 Cortex-A9，DDR 512 MB，PS UART1 接 COM7）
状态：**已在板子上跑通并做过 45 秒稳定性抓取**（见第 10 节）

## 1. 验证目标

| 验证点 | 判据 | 实测 |
|---|---|---|
| 两核并行运行 | 串口交错出现 `[CPU0] heartbeat #n` 与 `[CPU1] heartbeat #n`，序号各自递增 | 通过 |
| 共用 UART1 的互斥 | 每行输出完整、不与其他核字符混排 | 通过（0 坏行） |
| 两核运行在不同 DDR 段 | CPU0 代码/栈 `0x00100000` 起，CPU1 `0x00300000` 起，互不覆盖 | 通过 |
| 一个共享地址空间完成数据控制 | 共享区 `0x10000000`：心跳计数 + 命令信箱；CPU0 发 `ping N`，CPU1 回 `pong N` | 通过（23/23） |
| 串口参数 | PS UART1（`0xE0001000`），115200-8N1 | 通过（COM7） |

## 2. 工程结构

| 工程 | 类型 | 绑定内核 / Domain | 说明 |
|---|---|---|---|
| `platform` | 平台 | `standalone_ps7_cortexa9_0`、`core2` | 由 `Sys_wrapper.xsa` 生成，BSP 已构建 |
| `platform/zynq_fsbl` | FSBL | CPU0 | 调试时完成 PS/DDR/时钟/UART 初始化 |
| `hello_world` | 应用 | `ps7_cortexa9_0`（CPU0） | 初始化共享区、心跳、发起 ping |
| `hello_world_1` | 应用 | `ps7_cortexa9_1`（CPU1） | 等 CPU0 初始化、心跳、应答 ping |
| `amp_system` | System | 上述两个应用 | 一个调试配置同时加载两核 |

公共代码只有一份：

```
amp_common.h                     ← 唯一权威版本：地址、共享块、互斥、UART、延时、信箱
hello_world/src/amp_common.h     ← 转发头：#include "../../amp_common.h"
hello_world_1/src/amp_common.h   ← 转发头：同上
hello_world/src/helloworld.c     ← CPU0
hello_world_1/src/helloworld.c   ← CPU1
run_amp.tcl                      ← xsct 一键下载/运行脚本
build_amp.ps1                    ← 命令行增量编译脚本
```

当前构建产物（`-Wall -Wextra` 零告警）：

| 文件 | 入口 | text / data / bss |
|---|---|---|
| `hello_world/build/hello_world.elf` | `0x00100000` | 29496 / 1144 / 22576 |
| `hello_world_1/build/hello_world_1.elf` | `0x00300000` | 28676 / 1144 / 22584 |

## 3. 内存布局

| 区域 | 地址范围 | 大小 | 用途 |
|---|---|---|---|
| CPU0 私有 | `0x00100000` ~ `0x00300000` | 2 MB | 代码、只读数据、data、bss、MMU 表、堆、栈 |
| CPU1 私有 | `0x00300000` ~ `0x00500000` | 2 MB | 同上 |
| 两核共享 | `0x10000000` ~ `0x100FFFFF` | 1 MB | 控制块 + 命令信箱 |

- 两段私有区由各自的 `lscript.ld` 固定，`Hello_worldExample.cmake` 里的
  `ps7_ddr_0` 也写成同样的 `0x100000;0x200000` / `0x300000;0x200000`，
  即使 IDE 重新生成链接脚本也不会退回整段 DDR。
- 共享区选在 **1 MB 边界**：Cortex-A9 的 `Xil_SetTlbAttributes()` 一次改一个
  1 MB 段，整块共享区正好对应一个段，两核都设为 `NORM_NONCACHE`
  （Normal Non-cacheable + shareable）。
- 不使用 OCM：数据控制集中在一个共享 DDR 地址空间里。

## 4. 共享控制块布局（`amp_shared_t`，版本 `0x00020200`）

| 偏移 | 字段 | 方向 | 含义 |
|---|---|---|---|
| `0x00` | `magic` | CPU0 写 | `0x414D5032`（"AMP2"） |
| `0x04` | `version` | CPU0 写 | `0x00020200` |
| `0x08` | `init_state` | CPU0 写 | 0=未就绪，`0x444F4E45`（"DONE"）=可用 |
| `0x0C` | `uart_turn` | 两核 | Peterson：当前轮到谁打印 |
| `0x10` | `uart_want[0]` | CPU0 写 | CPU0 想打印 |
| `0x14` | `uart_want[1]` | CPU1 写 | CPU1 想打印 |
| `0x18` | `lock_recoveries` | 两核 | 等待超时强制接管次数 |
| `0x1C` | `cpu1_ready` | CPU1 写 | CPU1 已进入主循环 |
| `0x20` | `cpu0_hb` | CPU0 写 | CPU0 心跳计数 |
| `0x24` | `cpu1_hb` | CPU1 写 | CPU1 心跳计数 |
| `0x28` | `seq` | CPU0 写 | 命令序号，0=无命令 |
| `0x2C` | `cmd` | CPU0 写 | `AMP_CMD_PING` |
| `0x30` | `arg` | CPU0 写 | 命令参数（= 序号） |
| `0x34` | `payload_len` | CPU0 写 | payload 字节数 |
| `0x38` | `payload[64]` | CPU0 写 | 命令附带数据（"ping"） |
| `0x78` | `ack_seq` | CPU1 写 | 已处理完的命令序号 |
| `0x7C` | `status` | CPU1 写 | 0=OK，1=不支持的命令 |
| `0x80` | `resp_val` | CPU1 写 | 应答附带数据（CPU1 当前心跳） |
| `0x84` | `resp_len` | CPU1 写 | resp 字节数 |
| `0x88` | `resp[64]` | CPU1 写 | 应答字符串（"pong N"） |

结构体总长 `0xC8`，全部 4 字节对齐，单字访问不会撕裂。

## 5. UART1 打印与互斥（Peterson 算法）

- UART1 基地址 `XPAR_XUARTPS_0_BASEADDR` = `0xE0001000`（本设计只启用 UART1，
  所以 BSP 里实例号是 0），输入时钟 100 MHz。
- `amp_uart_hw_init()`：8N1、`BAUDGEN=124`/`BAUDDIV=6`
  （100 MHz/(124×7)=115207，误差 0.006%），复位 FIFO、关中断、使能 TX/RX。
  只由 CPU0 执行一次。
- 发送轮询 `SR.TXFULL` 后写 `FIFO`，不依赖 BSP `printf`。
- **互斥粒度是整行**：`amp_printf()` 先在栈上格式化整行，再一次性取锁发送，
  不会出现半行交错。

### 为什么不用 LDREX/STREX

板子上实测到一个硬问题：两个 A9 同时对同一个 Normal Non-cacheable 地址做
`LDREX/STREX`，会不断清掉对方的独占监视器，双方一起卡在 `strex` 重试环里
（调试器读回两个核的 PC 都在 `strex` 之后，锁值被失败尝试写成 1 却无人持有）。
这是 LL/SC 活锁，加只读退避或超时都不能根治。

因此 UART 互斥改为只依赖普通读写的 **Peterson 双进程算法**：

```
进入区：want[me] = 1;  turn = other;  dmb;
等待区：while (want[other] && turn == other) ;
临界区：整行发送
退出区：want[me] = 0;
```

- 共享区是非缓存且 shareable 的，普通 load/store 直接落到物理内存，配合 `dmb`
  即可保证顺序；
- 不需要独占访问，因此没有 LL/SC 活锁；
- 两核启动顺序无关；单核运行时对方 `want=0`，直接进入；
- 等待超过约 2000 万次（秒级）会打印
  `[AMP] WARN: uart mutex timeout, forced takeover` 并清掉对方意愿标志接管，
  用于“某核被调试器停住”时保住本核输出（尽力恢复）。

## 6. 共享区初始化（固定由 CPU0 完成）

```
CPU0：init_state=0 → 清空控制块 → amp_uart_hw_init()
      → version=0x00020200 → magic="AMP2" → init_state="DONE"
CPU1：轮询直到 init_state=="DONE" 且 magic/version 匹配
```

- 早先“两核抢初始化权”的 CAS 版本在复位后 DDR 残留 `BUSY`、或两核同时 CAS 时，
  会双双卡在等待里；现在把初始化职责固定给 CPU0，最简单、可重复。
- 调试流程（System Debugger / `run_amp.tcl`）总是先启动 CPU0，因此这是安全的。
- 只启动 CPU1 时它会一直等 CPU0（约 2000 万次后打印一次
  `[AMP] WARN: waiting for CPU0 init...` 并继续等）；本验证要求双核，属预期行为。
- 每次 CPU0 启动都会完整复位共享块，所以重新下载不会受上次残留影响。

## 7. 命令信箱协议（单生产者 / 单消费者）

```
CPU0                                        CPU1
----                                        ----
ack_seq == seq-1 ?
  是：写 payload / cmd / arg
      seq = N            (release)
                                            seq != 0 ? (acquire)
                                              读 cmd / arg / payload
                                              打印 [CPU1] recv ...
                                              写 resp / resp_val / status
                                              seq = 0        (release)
                                              ack_seq = N    (release)
等待 ack_seq == N (500 ms 超时)
读 resp，打印 [CPU0] ping #N -> pong N
```

- 同一时刻只有一条命令在途；`ack_seq` 保证“上一条处理完才发下一条”。
- 先清 `seq` 再写 `ack_seq`，CPU0 看到 ack 时 CPU1 一定已读完旧命令。
- 超时不会强行发下一条，等 CPU1 恢复应答后自动继续。
- 信箱不需要锁：写方只有 CPU0，读方只有 CPU1，用 acquire/release 保证顺序。

## 8. 构建

### 8.1 IDE

1. 打开 Vitis 2024.1，选择本工作空间。
2. 右键 `platform` → **Build Platform**（两个 domain：
   `standalone_ps7_cortexa9_0`、`core2`）。
3. 右键 `hello_world` → **Build**；`hello_world_1` → **Build**。
4. 右键 `amp_system` → **Build**（可选）。

### 8.2 命令行

```powershell
powershell -ExecutionPolicy Bypass -File .\build_amp.ps1
```

脚本给 PATH 加上工具链后对两个 `build` 目录执行 `ninja`；已实测通过
（注意收尾的 `arm-none-eabi-size` 需要工具链在 PATH 里）。

## 9. 下载与运行

### 9.1 IDE（推荐日常使用）

`amp_system` → `amp_system_app_hw_1` → Launch。配置会复位系统、下载比特流、
跑 FSBL、把两个 ELF 分别装到 A9 #0/#1。串口接 **UART1**，115200-8N1。

### 9.2 命令行 xsct（本次验证使用）

```powershell
$env:XILINX_VITIS = 'D:\xilinx2024.1\VITIS_SDK\Vitis\2024.1'
& "$env:XILINX_VITIS\bin\xsct.bat" .\run_amp.tcl
```

`run_amp.tcl` 依次执行：

1. `rst -system` 复位 PS（DDR 内容丢失）；
2. `fpga -file amp_system/_ide/bitstream/Sys_wrapper.bit`；
3. `source ps7_init.tcl` + `ps7_init` + `ps7_post_config`；
4. 下载两个 ELF 到 A9 #0 / #1；
5. 两个核分别 `con`。

> 串口助手打开 COM7 时，其它程序无法同时读该端口；用脚本抓串口前需先关闭助手。

## 10. 上板实测结果（2026-11-15，COM7）

45 秒连续抓取（日志 `amp_serial_capture.log`，181 行）：

| 指标 | 结果 |
|---|---|
| 非空行 | 181 |
| 坏行（不符合 `[CPUx]` 行格式） | **0** |
| `WARN` / `timeout` / `forced` | **0** |
| CPU0 心跳 | 45 行，序号 26→70 严格递增（1 秒/条） |
| CPU1 心跳 | 90 行，序号 50→139 严格递增（0.5 秒/条） |
| ping/pong | 23 组，序号 13→35 连续，`ping #N -> pong N` 全部一致 |
| `[CPU1] recv #N` 与 `[CPU0] ping #N` | 23/23 一一对应 |
| 行内夹带第二个 `[CPU` | 0（整行互斥有效） |
| RTT | 约 8.9~9.4 ms（含 CPU1 的 recv 打印和 10 ms 轮询周期） |

实际输出样例：

```
============================================================
[CPU0] ZYNQ7010 AMP bring-up, PS UART1 115200-8N1
[CPU0] private DDR : 0x00100000 - 0x00300000
[CPU0] shared  DDR : 0x10000000 (1 MB, non-cacheable)
[CPU0] up, waiting for CPU1 ...
============================================================
[CPU1] ZYNQ7010 AMP bring-up, PS UART1 115200-8N1
[CPU1] private DDR : 0x00300000 - 0x00500000
[CPU1] shared  DDR : 0x10000000 (1 MB, non-cacheable)
[CPU1] up, ready
[CPU0] CPU1 ready, AMP test start
[CPU0] heartbeat #1  (cpu1_hb=0)
[CPU1] heartbeat #1  (cpu0_hb=1)
[CPU1] heartbeat #2  (cpu0_hb=1)
[CPU0] heartbeat #2  (cpu1_hb=2)
[CPU1] recv #1: ping
[CPU0] ping #1 -> pong 1 (cpu1_hb=2, rtt=4066 us)
...
[CPU1] heartbeat #94  (cpu0_hb=47)
[CPU0] heartbeat #48  (cpu1_hb=94)
[CPU1] recv #24: ping
[CPU0] ping #24 -> pong 24 (cpu1_hb=94, rtt=9063 us)
```

## 11. 验证清单

- [x] 两核都有 banner，且都持续输出 `heartbeat`。
- [x] 心跳行从不出现半行/字符穿插（0 坏行）。
- [x] `(cpu1_hb=...)`、`(cpu0_hb=...)` 持续增长，双方能互相跟上。
- [x] 每 2 秒一组 `[CPU1] recv #N` 与 `[CPU0] ping #N -> pong N`，序号一致。
- [x] 45 秒连续运行无 `timeout`、无 `WARN`。
- [ ] 长时间（>2 小时）老化测试（可选）。
- [x] 私有 DDR 段不重叠：ELF 入口 `0x100000`/`0x300000`，栈顶均在各自 2 MB 内。

## 12. 常见问题排查

| 现象 | 原因 / 处理 |
|---|---|
| 串口完全无输出 | ① 确认接的是 **UART1**（`0xE0001000`），不是 UART0；② 115200-8N1，TX/RX 未接反、共地；③ Vivado PS 配置里 UART1 的 MIO 与板卡一致；④ 若 FSBL 阶段就没有打印，先查硬件/引脚 |
| 只有 CPU0 打印 | CPU1 的 ELF 未加载或未运行：检查 `run_amp.tcl`/System 配置里是否下载到 `ps7_cortexa9_1`，确认入口是 `0x00300000` |
| CPU1 一直停在等待 | CPU0 没起来：看是否有 `[AMP] WARN: waiting for CPU0 init...`；确认先启动 CPU0 |
| `[AMP] WARN: uart mutex timeout, forced takeover` | 另一核被调试器停住/跑飞超过秒级；本核强制接管继续输出。正常双核运行不应出现（本次 45 秒 0 次） |
| `[CPU0] WARN: CPU1 not ready in 5 s` | CPU1 未启动/跑飞；CPU0 会继续心跳，CPU1 起来后自动开始 ping |
| `[CPU0] ping #N timeout` 后不再 ping | 单命令在途：CPU1 恢复并回 ack 后自动继续 |
| 下载时报 `Cannot access DDR: the controller is held in reset` | PS/DDR 未初始化：确认走 FSBL 或 `ps7_init`，比特流已下载；必要时断电重连 JTAG |
| 重新下载后行为异常 | 用 `run_amp.tcl` 的 `rst -system`（或 System 配置的 resetSystem）；CPU0 每次启动都会重置共享块 |
| 重新生成平台后两核跑飞 | 检查两个 `Hello_worldExample.cmake` 的 DDR 段（`0x100000;0x200000` / `0x300000;0x200000`）与 `lscript.ld` |
| 编译报 `'-fno-tree-loop-distribute-patterns-DXIL_DISABLE_CACHE'` | 两个编译选项粘连；Extra Compiler Flags 应为 `-g -Wall -Wextra -fno-tree-loop-distribute-patterns` |
| `'arm-none-eabi-size' is not recognized` | 命令行构建时 PATH 缺少工具链 `bin`；用 `build_amp.ps1` |
| 串口被占用（`对端口 COM7 的访问被拒绝`） | 先关闭串口助手/Vitis 终端；Windows 串口默认独占 |

## 13. 关键实现说明

- **启动模型**：调试器分别下载两个 ELF 并直接启动 CPU1，代码不操作 SLCR
  释放复位、不做 `wfe/sev`。
- **私有段隔离**：两个 `lscript.ld` 的 `ps7_ddr_0` 起点不同，各 2 MB；
  栈、堆、MMU 表都在自己段内。
- **共享段属性**：`NORM_NONCACHE`（Normal Non-cacheable + shareable），
  两核访问同一份物理内存；D-cache 保持开启，只对共享段关缓存。
- **UART 互斥**：Peterson 算法（普通读写 + `dmb`），规避 A9 双核
  `LDREX/STREX` 活锁；整行一次持锁发送。
- **初始化**：CPU0 固定负责，每次启动重置共享块；CPU1 只等 `DONE`。
- **延时**：A9 全局定时器（`0xF8F00200`，CPU/2 = 333 MHz）做微秒延时，
  心跳节拍准确；定时器异常时按时钟判断 + 自旋上限双重兜底。
- **心跳数据**：每核只写自己的 `cpuX_hb`，读对方的，32 位单字访问不撕裂。

## 14. 文件清单

| 文件 | 说明 |
|---|---|
| `amp_common.h` | 公共定义、共享块、Peterson 互斥、UART、延时、`amp_printf`、信箱 |
| `hello_world/src/amp_common.h` | 转发到根目录头文件 |
| `hello_world_1/src/amp_common.h` | 转发到根目录头文件 |
| `hello_world/src/helloworld.c` | CPU0：初始化、心跳、发起 ping、打印 pong |
| `hello_world_1/src/helloworld.c` | CPU1：等待初始化、心跳、应答 ping |
| `run_amp.tcl` | xsct 一键复位/下载/运行 |
| `build_amp.ps1` | 命令行增量编译两个应用 |
| `amp_serial_capture.log` | 45 秒上板实测串口日志 |
| `amp_system/_ide/.theia/launch.json` | 双核 System 调试配置 |

> 旧版遗留的 `amp_share_nolock.h`、`mutex_uart.h`、`hello_world/mutex.h` 已删除，
> 没有任何工程引用它们。
