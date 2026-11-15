/******************************************************************************
 * helloworld.c  —  CPU0 (ps7_cortexa9_0)
 *
 * ZYNQ7010 双核 Cortex-A9 AMP 裸跑验证 —— CPU0 侧：
 *   1. 初始化共享区（与 CPU1 竞争初始化权，只有一核真正执行）；
 *   2. 等待 CPU1 上报就绪；
 *   3. 每 1 s 打印一条心跳，并顺带打印从共享区读到的 CPU1 心跳计数；
 *   4. 每 2 s 通过共享信箱给 CPU1 发一条 ping 命令，等待 pong 应答并打印往返时间。
 *
 * 私有代码/数据/栈/堆：DDR 0x00100000 ~ 0x00300000（2 MB，见 lscript.ld）
 * 共享区：             DDR 0x10000000 起 1 MB，Normal Non-cacheable
 *
 * 启动模型：JTAG 调试器分别下载两个 ELF，CPU1 由调试器直接启动，
 *           本代码不操作 SLCR 释放复位、不做 wfe/sev。
 ******************************************************************************/

#include "xil_io.h"
#include "xil_mmu.h"
#include "amp_common.h"

#define AMP_READY_TIMEOUT_MS   5000U   /* 等 CPU1 就绪的上限 */
#define AMP_PING_TIMEOUT_MS    500U    /* 等 pong 的上限（一行串口输出约 4 ms） */

int main(void)
{
    uint32_t tick = 0U;
    uint32_t cmd_seq = 0U;
    uint32_t cpu1_ready;

    amp_timer_init();

    /* 共享区所在的 1 MB 段设为 Normal Non-cacheable，之后直接按物理地址访问 */
    Xil_SetTlbAttributes(AMP_SHARE_BASE, NORM_NONCACHE);
    (void)amp_share_attach(0U);

    amp_puts("\r\n");
    amp_puts("============================================================\r\n");
    amp_puts("[CPU0] ZYNQ7010 AMP bring-up, PS UART1 115200-8N1\r\n");
    amp_puts("[CPU0] private DDR : 0x00100000 - 0x00300000\r\n");
    amp_puts("[CPU0] shared  DDR : 0x10000000 (1 MB, non-cacheable)\r\n");
    amp_puts("[CPU0] up, waiting for CPU1 ...\r\n");

    /* 等待 CPU1 就绪；超时也不死等，继续打心跳，便于定位 CPU1 侧问题 */
    (void)amp_wait_flag(&AMP_SHARE->cpu1_ready, 1U, AMP_READY_TIMEOUT_MS);

    cpu1_ready = __atomic_load_n(&AMP_SHARE->cpu1_ready, __ATOMIC_ACQUIRE);
    if (cpu1_ready == 1U) {
        amp_puts("[CPU0] CPU1 ready, AMP test start\r\n");
    } else {
        amp_puts("[CPU0] WARN: CPU1 not ready in 5 s, heartbeats only\r\n");
    }

    for (;;) {
        uint32_t cpu1_hb;

        /* ---- 心跳：写自己的计数，读对方计数，一行一次锁 ---- */
        tick++;
        __atomic_store_n(&AMP_SHARE->cpu0_hb, tick, __ATOMIC_RELEASE);
        cpu1_hb = __atomic_load_n(&AMP_SHARE->cpu1_hb, __ATOMIC_ACQUIRE);
        amp_printf("[CPU0] heartbeat #%u  (cpu1_hb=%u)\r\n", tick, cpu1_hb);

        /* ---- 每 2 拍发一条 ping，验证共享信箱双向通信 ---- */
        cpu1_ready = __atomic_load_n(&AMP_SHARE->cpu1_ready, __ATOMIC_ACQUIRE);
        if ((cpu1_ready == 1U) && ((tick & 1U) == 0U)) {
            uint32_t next = cmd_seq + 1U;

            if (amp_send_cmd(next, AMP_CMD_PING, next, "ping") != 0U) {
                uint64_t t0 = amp_ticks();

                cmd_seq = next;

                if (amp_wait_ack(next, AMP_PING_TIMEOUT_MS) != 0U) {
                    char resp[AMP_MSG_MAX + 1U];
                    uint32_t rtt_us;

                    (void)amp_read_resp(resp, (uint32_t)sizeof(resp));
                    rtt_us = (uint32_t)((amp_ticks() - t0) / (uint64_t)AMP_TICKS_PER_US);
                    amp_printf("[CPU0] ping #%u -> %s (cpu1_hb=%u, rtt=%u us)\r\n",
                               next, resp,
                               __atomic_load_n(&AMP_SHARE->resp_val, __ATOMIC_ACQUIRE),
                               rtt_us);
                } else {
                    amp_printf("[CPU0] ping #%u timeout, CPU1 may be stopped\r\n", next);
                }
            }
        }

        amp_delay_ms(1000U);
    }

    return 0;
}
