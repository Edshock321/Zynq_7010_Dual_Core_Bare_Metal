/******************************************************************************
 * amp_common.h
 *
 * ZYNQ7010（XC7Z010，双 Cortex-A9）AMP 裸跑验证 —— 两核共用的共享定义与内联接口。
 *
 * 地址空间（已与两个工程的 lscript.ld 核对，互不重叠）：
 *   CPU0 私有（hello_world）  : DDR 0x00100000 ~ 0x00300000  2 MB
 *   CPU1 私有（hello_world_1）: DDR 0x00300000 ~ 0x00500000  2 MB
 *   两核共享                  : DDR 0x10000000 ~ 0x100FFFFF  1 MB
 *
 * 共享区起点在 1 MB 边界上，两核都在 MMU 中把该 1 MB 段设为 Normal
 * Non-cacheable（见 NORM_NONCACHE），所以控制块和信箱的读写不需要 cache
 * 维护，直接读写物理内存即可；普通 load/store 不经过 cache。
 *
 * 访问规则：
 *   - UART 打印通过共享控制块里的 Peterson 双进程互斥（普通读写 + dmb）；
 *   - 命令信箱是单生产者/单消费者：CPU0 只写命令侧，CPU1 只写应答侧；
 *   - 所有跨核可见的“置位/清零”都带 acquire/release 语义。
 *
 * 启动模型：Vitis 调试器经 JTAG 分别把两个 ELF 下载到各自内核并复位运行，
 *           CPU1 由调试器直接启动，因此代码里不做 SLCR 释放复位 / wfe-sev 唤醒。
 ******************************************************************************/
#ifndef AMP_COMMON_H
#define AMP_COMMON_H

#include <stdint.h>
#include <stdarg.h>

#include "xparameters.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xil_types.h"
#include "xuartps_hw.h"

/* ======================== 1. 地址空间划分 ======================== */

#define AMP_CPU0_DDR_BASE     0x00100000U   /* CPU0 私有段起始 */
#define AMP_CPU1_DDR_BASE     0x00300000U   /* CPU1 私有段起始 */
#define AMP_DDR_REGION_SIZE   0x00200000U   /* 每核 2 MB 私有段 */

#define AMP_SHARE_BASE        0x10000000U   /* 两核共享区（1 MB 段首地址） */
#define AMP_SHARE_SIZE        0x00100000U   /* 共享区大小，正好一个 MMU 段 */

/* 编译期自检：段不重叠、共享区 1 MB 对齐、定时器分频不为 0 */
#if ((AMP_CPU0_DDR_BASE + AMP_DDR_REGION_SIZE) > AMP_CPU1_DDR_BASE)
#error "CPU0 私有段与 CPU1 私有段重叠"
#endif
#if ((AMP_CPU1_DDR_BASE + AMP_DDR_REGION_SIZE) > AMP_SHARE_BASE)
#error "私有段与共享段重叠"
#endif
#if ((AMP_SHARE_BASE & (AMP_SHARE_SIZE - 1U)) != 0U)
#error "共享区必须按 1 MB 对齐（Xil_SetTlbAttributes 一次改一个 1 MB 段）"
#endif

/* ======================== 2. 共享控制块 ======================== */

#define AMP_MSG_MAX           64U           /* 信箱 payload/resp 最大字节数 */

/*
 * 共享控制块：放在共享区起始处，两核都按同一份布局访问。
 * 每个字段都是 4 字节对齐的 32 位量，单次读写不会撕裂。
 */
typedef struct {
    /* ---- 初始化与互斥 ---- */
    volatile uint32_t magic;            /* AMP_SHARE_MAGIC，共享区有效标志       */
    volatile uint32_t version;          /* AMP_SHARE_VERSION，布局版本           */
    volatile uint32_t init_state;       /* AMP_INIT_IDLE / AMP_INIT_DONE          */
    volatile uint32_t uart_turn;        /* Peterson：当前轮到谁打印              */
    volatile uint32_t uart_want[2];     /* Peterson：[0]=CPU0 想打印，[1]=CPU1   */
    volatile uint32_t lock_recoveries;  /* 等待超时强制接管的累计次数            */

    /* ---- 心跳状态（每核只写自己的字段，另一核只读） ---- */
    volatile uint32_t cpu1_ready;       /* CPU1 初始化完成、进入主循环           */
    volatile uint32_t cpu0_hb;          /* CPU0 心跳计数                         */
    volatile uint32_t cpu1_hb;          /* CPU1 心跳计数                         */

    /* ---- CPU0 -> CPU1 命令侧 ---- */
    volatile uint32_t seq;              /* 命令序号，0=无命令；非 0 即有效       */
    volatile uint32_t cmd;              /* AMP_CMD_xxx                           */
    volatile uint32_t arg;              /* 命令参数                              */
    volatile uint32_t payload_len;      /* payload 有效字节数                    */
    uint8_t           payload[AMP_MSG_MAX];

    /* ---- CPU1 -> CPU0 应答侧 ---- */
    volatile uint32_t ack_seq;          /* 已处理完的命令序号                    */
    volatile uint32_t status;           /* AMP_STATUS_xxx                        */
    volatile uint32_t resp_val;         /* 应答附带的数据（此处为 CPU1 心跳）    */
    volatile uint32_t resp_len;         /* resp 有效字节数                       */
    uint8_t           resp[AMP_MSG_MAX];
} amp_shared_t;

#define AMP_SHARE   ((amp_shared_t *)AMP_SHARE_BASE)

/* 共享区有效标志 / 布局版本 */
#define AMP_MAGIC             0x414D5032U   /* "AMP2" */
#define AMP_SHARE_VERSION     0x00020200U   /* v2.2.0：互斥改为 Peterson，布局有变 */

/* 初始化状态：0=未就绪，DONE=CPU0 已完成共享区和 UART 初始化 */
#define AMP_INIT_IDLE         0x00000000U
#define AMP_INIT_DONE         0x444F4E45U   /* "DONE" */

/* 命令字 / 状态码 */
#define AMP_CMD_NONE          0U
#define AMP_CMD_PING          1U
#define AMP_STATUS_OK         0U
#define AMP_STATUS_BAD_CMD    1U

/* ======================== 3. 时间基准（A9 全局定时器） ======================== */

/*
 * Cortex-A9 MPCore 全局定时器固定在 0xF8F00200，计数频率 = CPU 时钟 / 2。
 * 直接按寄存器访问，不依赖 BSP 的 xtime_l.h（该头文件不在应用 include 路径里）。
 */
#ifndef XPAR_GLOBAL_TIMER_BASEADDR
#define XPAR_GLOBAL_TIMER_BASEADDR   0xF8F00200U
#endif
#define AMP_GTIMER_BASE       XPAR_GLOBAL_TIMER_BASEADDR
#define AMP_GTIMER_CNT_LO     (AMP_GTIMER_BASE + 0x00U)
#define AMP_GTIMER_CNT_HI     (AMP_GTIMER_BASE + 0x04U)
#define AMP_GTIMER_CTRL       (AMP_GTIMER_BASE + 0x08U)

/*
 * 本设计的 PS 主频 666.666687 MHz（两个 domain 的 xparameters.h 都给出
 * XPAR_CPU_CORE_CLOCK_FREQ_HZ）。注意不要用 XPAR_CPU_CORTEXA9_CORE_CLOCK_FREQ_HZ：
 * SDT 流程里它被定义成 XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ，而后者并未生成。
 */
#if defined(XPAR_CPU_CORE_CLOCK_FREQ_HZ)
#define AMP_CPU_HZ            XPAR_CPU_CORE_CLOCK_FREQ_HZ
#else
#define AMP_CPU_HZ            666666687U    /* 改 PS 时钟时同步修改 */
#endif

/* 定时器每微秒的计数（CPU/2 -> 333 @ 666.666 MHz） */
#define AMP_TICKS_PER_US_RAW  (AMP_CPU_HZ / 2000000U)

#if (AMP_TICKS_PER_US_RAW == 0)
#error "CPU 时钟宏异常，全局定时器分频为 0"
#endif

#define AMP_TICKS_PER_US      ((uint32_t)AMP_TICKS_PER_US_RAW)

/* 打开全局定时器（复位后默认关闭；FSBL 通常已打开，这里只兜底） */
static inline void amp_timer_init(void)
{
    if ((Xil_In32(AMP_GTIMER_CTRL) & 0x1U) == 0U) {
        Xil_Out32(AMP_GTIMER_CTRL, 0x1U);   /* ENABLE=1，PRESCALER=0 */
    }
}

/* 读 64 位全局定时器计数（高位回读，避免进位撕裂） */
static inline uint64_t amp_ticks(void)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t hi2;

    do {
        hi  = Xil_In32(AMP_GTIMER_CNT_HI);
        lo  = Xil_In32(AMP_GTIMER_CNT_LO);
        hi2 = Xil_In32(AMP_GTIMER_CNT_HI);
    } while (hi != hi2);

    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* 微秒级延时；定时器异常时按循环上限退出，不会死等 */
static inline void amp_delay_us(uint32_t us)
{
    uint64_t start;
    uint64_t delta;
    uint32_t guard;

    if (us == 0U) {
        return;
    }

    start = amp_ticks();
    delta = (uint64_t)us * (uint64_t)AMP_TICKS_PER_US;
    guard = (us * 500U) + 10000U;

    while ((amp_ticks() - start) < delta) {
        if (guard == 0U) {
            break;
        }
        guard--;
    }
}

static inline void amp_delay_ms(uint32_t ms)
{
    while (ms != 0U) {
        amp_delay_us(1000U);
        ms--;
    }
}

/*
 * 等待共享标志到达期望值。超时用定时器判断，另加自旋上限兜底：
 * 万一全局定时器没有走动（计数恒为 0），也不会死等。
 * 返回 1 = 等到了，0 = 超时。
 */
static inline uint32_t amp_wait_flag(volatile uint32_t *flag, uint32_t value,
                                     uint32_t timeout_ms)
{
    uint64_t start = amp_ticks();
    uint64_t limit = (uint64_t)timeout_ms * 1000U * (uint64_t)AMP_TICKS_PER_US;
    uint32_t spins = 0U;

    while (__atomic_load_n(flag, __ATOMIC_ACQUIRE) != value) {
        spins++;
        if (((amp_ticks() - start) > limit) || (spins > 50000000U)) {
            return 0U;
        }
    }
    return 1U;
}

/* ======================== 4. UART1 底层（轮询寄存器） ======================== */

#define AMP_UART_BASE         XPAR_XUARTPS_0_BASEADDR   /* 本设计只启用 UART1 @0xE0001000 */
#define AMP_UART_CLK          XPAR_XUARTPS_0_CLOCK_FREQ /* UART 输入时钟 100 MHz           */
#define AMP_UART_BAUD         115200U

/*
 * 最小化 UART1 初始化：8N1、115200、使能 TX/RX、复位 FIFO、关中断。
 * 只由“共享区初始化者”调用一次，避免两核同时复位 FIFO 打乱输出。
 * 寄存器序列对齐官方 XUartPs_CfgInitialize / SetBaudRate。
 */
static inline void amp_uart_hw_init(void)
{
    uint32_t best_brgr = 124U;   /* 100MHz/(124*7) = 115207，误差 0.006% */
    uint32_t best_div  = 6U;
    uint32_t best_err  = 0xFFFFFFFFU;
    uint32_t d;
    uint32_t brgr;
    uint32_t calc;
    uint32_t err;

    /* 分频搜索：baud = CLK / (BRGR * (BAUDDIV + 1))，BAUDDIV ∈ [4, 254] */
    for (d = 4U; d < 255U; d++) {
        brgr = AMP_UART_CLK / (AMP_UART_BAUD * (d + 1U));
        if (brgr == 0U) {
            brgr = 1U;
        }
        calc = AMP_UART_CLK / (brgr * (d + 1U));
        err = (AMP_UART_BAUD > calc) ? (AMP_UART_BAUD - calc) : (calc - AMP_UART_BAUD);
        if (err < best_err) {
            best_err = err;
            best_brgr = brgr;
            best_div = d;
        }
    }

    /* 先关闭 TX/RX，避免配置过程中产生毛刺 */
    Xil_Out32(AMP_UART_BASE + XUARTPS_CR_OFFSET,
              XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS);

    /* 8 数据位、1 停止位、无校验、正常模式 */
    Xil_Out32(AMP_UART_BASE + XUARTPS_MR_OFFSET,
              XUARTPS_MR_CHARLEN_8_BIT | XUARTPS_MR_STOPMODE_1_BIT |
              XUARTPS_MR_PARITY_NONE | XUARTPS_MR_CHMODE_NORM);

    /* 写波特率分频 */
    Xil_Out32(AMP_UART_BASE + XUARTPS_BAUDGEN_OFFSET, best_brgr);
    Xil_Out32(AMP_UART_BASE + XUARTPS_BAUDDIV_OFFSET, best_div);

    /* TX/RX 逻辑与 FIFO 复位 */
    Xil_Out32(AMP_UART_BASE + XUARTPS_CR_OFFSET,
              XUARTPS_CR_TXRST | XUARTPS_CR_RXRST);

    /* 关闭全部中断，轮询模式 */
    Xil_Out32(AMP_UART_BASE + XUARTPS_IDR_OFFSET, XUARTPS_IXR_MASK);

    /* 使能 TX/RX */
    Xil_Out32(AMP_UART_BASE + XUARTPS_CR_OFFSET,
              XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN);
}

/* 发一个字节：等 TX FIFO 有空间（对齐官方驱动，等 TXFULL 清零） */
static inline void amp_uart_putc_nolock(uint8_t ch)
{
    while ((Xil_In32(AMP_UART_BASE + XUARTPS_SR_OFFSET) & XUARTPS_SR_TXFULL) != 0U) {
        __asm__ volatile ("nop");
    }
    Xil_Out32(AMP_UART_BASE + XUARTPS_FIFO_OFFSET, ch);
}

static inline void amp_uart_puts_nolock(const char *s)
{
    while (*s != '\0') {
        amp_uart_putc_nolock((uint8_t)*s);
        s++;
    }
}

/* ======================== 5. UART 打印互斥（Peterson 算法） ======================== */

/*
 * Cortex-A9 实测结论：两个核同时对同一个 Normal Non-cacheable 地址做
 * LDREX/STREX，会不断清掉对方的独占监视器，双方一起卡在 STREX 重试环里
 * （PC 停在 strex 后，锁值被失败尝试写成 1 却没有任何核真正持有）。
 * 这是 LL/SC 活锁，加超时也治不好，所以 UART 互斥不使用独占指令，
 * 改用只依赖普通读写的 Peterson 双进程算法：
 *
 *   want[me] = 1;  turn = other;  dmb;
 *   while (want[other] && turn == other) ;
 *       ... 临界区 ...
 *   want[me] = 0;
 *
 * 共享区已经是 Normal Non-cacheable + shareable，普通 load/store 直接
 * 落到物理内存，配合 dmb 即可保证顺序；两核启动顺序、单核运行都不受影响。
 */

#define AMP_LOCK_SPIN_MAX     20000000U

/* 本核编号：0 = CPU0，1 = CPU1，由 amp_share_attach() 最先设置 */
static uint32_t amp_this_cpu = 0U;

static inline void amp_uart_lock(void)
{
    amp_shared_t *sh = AMP_SHARE;
    uint32_t me = amp_this_cpu;
    uint32_t other = me ^ 1U;
    uint32_t spins = 0U;

    /* 进入区：先声明自己想打印，再把 turn 让给对方 */
    __atomic_store_n(&sh->uart_want[me], 1U, __ATOMIC_RELEASE);
    __atomic_store_n(&sh->uart_turn, other, __ATOMIC_RELEASE);
    __sync_synchronize();

    /* 等待区：对方也想打印，且 turn 还在对方手里 */
    while ((__atomic_load_n(&sh->uart_want[other], __ATOMIC_ACQUIRE) != 0U) &&
           (__atomic_load_n(&sh->uart_turn, __ATOMIC_ACQUIRE) == other)) {
        spins++;
        if (spins >= AMP_LOCK_SPIN_MAX) {
            /* 对端可能被停住：清掉它的意愿标志并接管，保证本核还能输出 */
            sh->lock_recoveries++;
            amp_uart_puts_nolock("[AMP] WARN: uart mutex timeout, forced takeover\r\n");
            __atomic_store_n(&sh->uart_want[other], 0U, __ATOMIC_RELEASE);
            break;
        }
    }
}

static inline void amp_uart_unlock(void)
{
    __atomic_store_n(&AMP_SHARE->uart_want[amp_this_cpu], 0U, __ATOMIC_RELEASE);
}

/* 整串加锁输出：一次持锁发送完整一行，另一核不会插进字符中间 */
static inline void amp_uart_write(const char *s)
{
    amp_uart_lock();
    amp_uart_puts_nolock(s);
    amp_uart_unlock();
}

static inline void amp_puts(const char *s)
{
    amp_uart_write(s);
}

/* ======================== 6. 极简格式化输出 ======================== */

/* 追加字符串，始终保证 dst[pos] 落在 cap 内并以 '\0' 结尾 */
static inline uint32_t amp_str_put(char *dst, uint32_t pos, uint32_t cap, const char *s)
{
    if (s != NULL) {
        while ((*s != '\0') && ((pos + 1U) < cap)) {
            dst[pos] = *s;
            pos++;
            s++;
        }
    }
    if (pos < cap) {
        dst[pos] = '\0';
    }
    return pos;
}

/* 追加无符号数（十进制/十六进制），digits 决定进制字符集 */
static inline uint32_t amp_num_put(char *dst, uint32_t pos, uint32_t cap,
                                   uint32_t val, const char *digits, uint32_t base)
{
    char tmp[32];
    uint32_t n = 0U;
    uint32_t i;

    if (val == 0U) {
        if ((pos + 1U) < cap) {
            dst[pos] = '0';
            pos++;
        }
        if (pos < cap) {
            dst[pos] = '\0';
        }
        return pos;
    }

    while ((val != 0U) && (n < sizeof(tmp))) {
        tmp[n] = digits[val % base];
        n++;
        val /= base;
    }
    for (i = 0U; (i < n) && ((pos + 1U) < cap); i++) {
        dst[pos] = tmp[n - 1U - i];
        pos++;
    }
    if (pos < cap) {
        dst[pos] = '\0';
    }
    return pos;
}

/*
 * 轻量 printf：支持 %s %u %d %x %%，一次调用只取一次锁。
 * 不支持宽度/精度等格式控制；整行超过 191 字节会被截断。
 */
static inline void amp_printf(const char *fmt, ...)
{
    char line[192];
    uint32_t n = 0U;
    va_list ap;

    va_start(ap, fmt);

    while ((*fmt != '\0') && ((n + 1U) < sizeof(line))) {
        if (*fmt != '%') {
            line[n] = *fmt;
            n++;
            fmt++;
            continue;
        }

        fmt++;
        if (*fmt == '\0') {
            line[n] = '%';
            n++;
            break;
        }

        switch (*fmt) {
        case '%':
            line[n] = '%';
            n++;
            break;
        case 's':
            n = amp_str_put(line, n, (uint32_t)sizeof(line), va_arg(ap, const char *));
            break;
        case 'u':
            n = amp_num_put(line, n, (uint32_t)sizeof(line),
                            va_arg(ap, uint32_t), "0123456789", 10U);
            break;
        case 'x':
            n = amp_num_put(line, n, (uint32_t)sizeof(line),
                            va_arg(ap, uint32_t), "0123456789abcdef", 16U);
            break;
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0) {
                if ((n + 1U) < sizeof(line)) {
                    line[n] = '-';
                    n++;
                }
                n = amp_num_put(line, n, (uint32_t)sizeof(line),
                                (uint32_t)(-(int64_t)v), "0123456789", 10U);
            } else {
                n = amp_num_put(line, n, (uint32_t)sizeof(line),
                                (uint32_t)v, "0123456789", 10U);
            }
            break;
        }
        default:
            if ((n + 1U) < sizeof(line)) {
                line[n] = '%';
                n++;
            }
            if ((n + 1U) < sizeof(line)) {
                line[n] = *fmt;
                n++;
            }
            break;
        }
        fmt++;
    }

    va_end(ap);

    if (n >= sizeof(line)) {
        n = (uint32_t)sizeof(line) - 1U;
    }
    line[n] = '\0';

    amp_uart_write(line);
}

/* ======================== 7. 共享区初始化（单核执行一次） ======================== */

/*
 * 两核启动后都调用本函数（cpu_id：CPU0 传 0，CPU1 传 1）。
 *
 * 初始化职责固定给 CPU0：
 *   - CPU0 每次启动都完整复位共享块 → 配置 UART → 写 version/magic → DONE；
 *   - CPU1 只等待 DONE（等不到就先自己配一下 UART 打一行告警，继续等）。
 *
 * 这样做是刻意的：早先“两核抢初始化权”的实现依赖 shared CAS，在
 * 复位后 DDR 残留 BUSY、或两核同时 CAS 时，会双双卡在等待里；
 * 而调试流程（System Debugger / run_amp.tcl）总是先启动 CPU0，
 * 固定由 CPU0 初始化最简单、可重复。
 *
 * 返回值：1 = 本核完成初始化（CPU0）；0 = CPU0 已完成（CPU1）。
 */
static inline uint32_t amp_share_attach(uint32_t cpu_id)
{
    amp_shared_t *sh = AMP_SHARE;
    uint32_t spins = 0U;

    amp_this_cpu = cpu_id & 1U;

    if (amp_this_cpu == 0U) {
        /* 第一件事：把状态置为“未就绪”，让可能先启动的 CPU1 继续等 */
        sh->init_state = AMP_INIT_IDLE;
        sh->magic = 0U;
        sh->version = 0U;

        sh->uart_turn = 0U;
        sh->uart_want[0] = 0U;
        sh->uart_want[1] = 0U;
        sh->lock_recoveries = 0U;
        sh->cpu1_ready = 0U;
        sh->cpu0_hb = 0U;
        sh->cpu1_hb = 0U;
        sh->seq = 0U;
        sh->cmd = AMP_CMD_NONE;
        sh->arg = 0U;
        sh->payload_len = 0U;
        sh->ack_seq = 0U;
        sh->status = AMP_STATUS_OK;
        sh->resp_val = 0U;
        sh->resp_len = 0U;

        amp_uart_hw_init();

        sh->version = AMP_SHARE_VERSION;
        sh->magic = AMP_MAGIC;
        __atomic_store_n(&sh->init_state, AMP_INIT_DONE, __ATOMIC_RELEASE);
        return 1U;
    }

    /* CPU1：等 CPU0 完成初始化。此时 UART 可能还没配置，先不打印 */
    for (;;) {
        if ((__atomic_load_n(&sh->init_state, __ATOMIC_ACQUIRE) == AMP_INIT_DONE) &&
            (sh->magic == AMP_MAGIC) &&
            (sh->version == AMP_SHARE_VERSION)) {
            return 0U;
        }

        spins++;
        if (spins == 20000000U) {
            /* 异常路径：CPU0 迟迟没起来，先打通串口给个提示，然后继续等 */
            amp_uart_hw_init();
            amp_uart_puts_nolock("[AMP] WARN: waiting for CPU0 init...\r\n");
        }
    }
}

/* ======================== 8. 命令信箱（单生产者/单消费者） ======================== */

/*
 * 发送一条命令。返回 1 = 已发出；0 = 上一条命令还没被应答，本次跳过。
 * 只有 CPU0 调用；seq 从 1 开始递增。
 */
static inline uint32_t amp_send_cmd(uint32_t seq, uint32_t cmd, uint32_t arg,
                                    const char *payload)
{
    amp_shared_t *sh = AMP_SHARE;
    uint32_t n = 0U;

    if (seq == 0U) {
        return 0U;
    }
    if (__atomic_load_n(&sh->ack_seq, __ATOMIC_ACQUIRE) != (seq - 1U)) {
        return 0U;
    }

    if (payload != NULL) {
        while ((n < (AMP_MSG_MAX - 1U)) && (payload[n] != '\0')) {
            sh->payload[n] = (uint8_t)payload[n];
            n++;
        }
    }
    sh->payload_len = n;
    sh->cmd = cmd;
    sh->arg = arg;

    __atomic_store_n(&sh->seq, seq, __ATOMIC_RELEASE);
    return 1U;
}

/* 取一条命令。返回序号（0 = 无命令）；只有 CPU1 调用 */
static inline uint32_t amp_recv_cmd(uint32_t *cmd, uint32_t *arg, uint32_t *len)
{
    amp_shared_t *sh = AMP_SHARE;
    uint32_t seq = __atomic_load_n(&sh->seq, __ATOMIC_ACQUIRE);

    if (seq == 0U) {
        return 0U;
    }

    *cmd = sh->cmd;
    *arg = sh->arg;
    *len = sh->payload_len;
    if (*len > AMP_MSG_MAX) {
        *len = AMP_MSG_MAX;
    }
    return seq;
}

/*
 * 回应一条命令。顺序很重要：
 *   先写 resp/status，再清 seq（允许 CPU0 发下一条），最后写 ack_seq。
 * CPU0 只有在看到 ack_seq 后才会覆盖 payload，因此不会读到半新半旧的数据。
 */
static inline void amp_reply(uint32_t seq, uint32_t status, uint32_t val,
                             const char *resp)
{
    amp_shared_t *sh = AMP_SHARE;
    uint32_t n = 0U;

    if (resp != NULL) {
        while ((n < (AMP_MSG_MAX - 1U)) && (resp[n] != '\0')) {
            sh->resp[n] = (uint8_t)resp[n];
            n++;
        }
    }
    sh->resp_len = n;
    sh->status = status;
    sh->resp_val = val;

    __atomic_store_n(&sh->seq, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&sh->ack_seq, seq, __ATOMIC_RELEASE);
}

/* 等待 ack_seq == seq；返回 1 = 已应答，0 = 超时 */
static inline uint32_t amp_wait_ack(uint32_t seq, uint32_t timeout_ms)
{
    return amp_wait_flag(&AMP_SHARE->ack_seq, seq, timeout_ms);
}

/* 读回应答字符串（自动截断并补 '\0'），返回有效长度 */
static inline uint32_t amp_read_resp(char *out, uint32_t cap)
{
    amp_shared_t *sh = AMP_SHARE;
    uint32_t n = sh->resp_len;
    uint32_t i;

    if (n > AMP_MSG_MAX) {
        n = AMP_MSG_MAX;
    }
    if (n > (cap - 1U)) {
        n = cap - 1U;
    }
    for (i = 0U; i < n; i++) {
        out[i] = (char)sh->resp[i];
    }
    out[n] = '\0';
    return n;
}

#endif /* AMP_COMMON_H */
