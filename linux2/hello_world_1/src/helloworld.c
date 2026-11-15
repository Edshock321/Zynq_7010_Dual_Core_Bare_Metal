/******************************************************************************
 * helloworld.c  —  CPU1 (ps7_cortexa9_1)
 *
 * ZYNQ7010 双核 Cortex-A9 AMP 裸跑验证 —— CPU1 侧：
 *   1. 初始化/挂接共享区（与 CPU0 竞争初始化权，只有一核真正执行）；
 *   2. 上报就绪标志；
 *   3. 每 0.5 s 打印一条心跳，并顺带打印从共享区读到的 CPU0 心跳计数；
 *   4. 每 10 ms 轮询共享信箱，收到 ping 命令就回写 pong 应答。
 *
 * 私有代码/数据/栈/堆：DDR 0x00300000 ~ 0x00500000（2 MB，见 lscript.ld）
 * 共享区：             DDR 0x10000000 起 1 MB，Normal Non-cacheable
 *
 * 启动模型：JTAG 调试器直接把本 ELF 下载到 CPU1 并启动，
 *           代码不做 wfe 等待；CPU0 通过共享就绪标志与本核握手。
 ******************************************************************************/

#include "xil_io.h"
#include "xil_mmu.h"
#include "amp_common.h"

#define AMP_HB_PERIOD_TICKS   50U   /* 50 x 10 ms = 500 ms 一条心跳 */
#define AMP_POLL_MS           10U   /* 信箱轮询周期 */

int main(void)
{
    uint32_t hb = 0U;
    uint32_t tick = 0U;

    amp_timer_init();

    /* 共享区所在的 1 MB 段设为 Normal Non-cacheable，之后直接按物理地址访问 */
    Xil_SetTlbAttributes(AMP_SHARE_BASE, NORM_NONCACHE);
    (void)amp_share_attach(1U);

    amp_puts("\r\n");
    amp_puts("============================================================\r\n");
    amp_puts("[CPU1] ZYNQ7010 AMP bring-up, PS UART1 115200-8N1\r\n");
    amp_puts("[CPU1] private DDR : 0x00300000 - 0x00500000\r\n");
    amp_puts("[CPU1] shared  DDR : 0x10000000 (1 MB, non-cacheable)\r\n");
    amp_puts("[CPU1] up, ready\r\n");

    /* 通知 CPU0：本核已进入主循环 */
    __atomic_store_n(&AMP_SHARE->cpu1_ready, 1U, __ATOMIC_RELEASE);

    for (;;) {
        uint32_t cmd;
        uint32_t arg;
        uint32_t len;
        uint32_t seq;

        /* ---- 处理一条命令（单生产者/单消费者，无需加锁） ---- */
        seq = amp_recv_cmd(&cmd, &arg, &len);
        if (seq != 0U) {
            char payload[AMP_MSG_MAX + 1U];
            char resp[24];
            uint32_t i;
            uint32_t n;

            if (len > AMP_MSG_MAX) {
                len = AMP_MSG_MAX;
            }
            for (i = 0U; i < len; i++) {
                payload[i] = (char)AMP_SHARE->payload[i];
            }
            payload[len] = '\0';

            amp_printf("[CPU1] recv #%u: %s\r\n", seq, payload);

            if (cmd == AMP_CMD_PING) {
                n = amp_str_put(resp, 0U, (uint32_t)sizeof(resp), "pong ");
                (void)amp_num_put(resp, n, (uint32_t)sizeof(resp),
                                  arg, "0123456789", 10U);
                amp_reply(seq, AMP_STATUS_OK, hb, resp);
            } else {
                amp_reply(seq, AMP_STATUS_BAD_CMD, hb, "bad cmd");
            }
        }

        /* ---- 心跳：写自己的计数，读对方计数，一行一次锁 ---- */
        tick++;
        if ((tick % AMP_HB_PERIOD_TICKS) == 0U) {
            uint32_t cpu0_hb;

            hb++;
            __atomic_store_n(&AMP_SHARE->cpu1_hb, hb, __ATOMIC_RELEASE);
            cpu0_hb = __atomic_load_n(&AMP_SHARE->cpu0_hb, __ATOMIC_ACQUIRE);
            amp_printf("[CPU1] heartbeat #%u  (cpu0_hb=%u)\r\n", hb, cpu0_hb);
        }

        amp_delay_ms(AMP_POLL_MS);
    }

    return 0;
}
