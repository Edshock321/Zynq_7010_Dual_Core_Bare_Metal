"""Generate the AMP architecture diagram (PNG) with Pillow."""

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "amp_arch_diagram.png"

W, H = 2000, 1100
INK = (38, 38, 38)
GRAY = (90, 90, 90)
ARROW = (64, 64, 64)

PS_FILL = (247, 251, 255)
PS_LINE = (31, 78, 121)
HOST_FILL = (237, 237, 237)
HOST_LINE = (110, 110, 110)
CPU_FILL = (255, 242, 204)
CPU_LINE = (191, 144, 0)
SH_FILL = (226, 239, 218)
SH_LINE = (84, 130, 53)
SUB_FILL = (255, 255, 255)
UART_FILL = (252, 228, 214)
UART_LINE = (197, 90, 17)
EXT_FILL = (221, 235, 247)
EXT_LINE = (46, 117, 182)
BADGE = (31, 78, 121)

REG = r"C:\Windows\Fonts\msyh.ttc"
BOLD = r"C:\Windows\Fonts\msyhbd.ttc"


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else REG, size)


img = Image.new("RGB", (W, H), "white")
d = ImageDraw.Draw(img)


def box(x, y, w, h, fill, line, width=3, radius=16):
    d.rounded_rectangle([x, y, x + w, y + h], radius=radius, fill=fill,
                        outline=line, width=width)


def text(x, y, s, f, fill=INK, spacing=8):
    d.multiline_text((x, y), s, font=f, fill=fill, spacing=spacing)


def center(x, y, s, f, fill=INK, spacing=8):
    bbox = d.multiline_textbbox((0, 0), s, font=f, spacing=spacing, align="center")
    w = bbox[2] - bbox[0]
    d.multiline_text((x - w / 2, y), s, font=f, fill=fill, spacing=spacing,
                     align="center")


def head(px, py, ang, size=16, color=ARROW):
    pts = [
        (px, py),
        (px - size * math.cos(ang - 0.42), py - size * math.sin(ang - 0.42)),
        (px - size * math.cos(ang + 0.42), py - size * math.sin(ang + 0.42)),
    ]
    d.polygon(pts, fill=color)


def arrow(p1, p2, width=4, both=False, color=ARROW):
    d.line([p1, p2], fill=color, width=width)
    ang = math.atan2(p2[1] - p1[1], p2[0] - p1[0])
    head(p2[0], p2[1], ang, color=color)
    if both:
        head(p1[0], p1[1], ang + math.pi, color=color)


def route(points, width=4, color=ARROW):
    for a, b in zip(points, points[1:]):
        d.line([a, b], fill=color, width=width)
    a, b = points[-2], points[-1]
    head(b[0], b[1], math.atan2(b[1] - a[1], b[0] - a[0]), color=color)


# ---------- title ----------
center(1000, 22, "ZYNQ7010 双核 AMP 验证方案示意图", font(40, True))
center(1000, 76,
       "Vitis 2024.1 · XC7Z010 · 两核独立 DDR + 1 MB 共享区 + PS UART1 115200-8N1",
       font(21), GRAY)

# ---------- host / JTAG ----------
box(700, 128, 600, 82, HOST_FILL, HOST_LINE)
center(1000, 145, "开发主机：Vitis 2024.1 IDE / xsct", font(23, True))
arrow((1000, 210), (1000, 284))
text(1022, 232, "① JTAG 下载：比特流 + ps7_init + 两个 ELF", font(20), INK)

# ---------- PS frame ----------
box(60, 286, 1880, 600, PS_FILL, PS_LINE, width=4, radius=20)
text(92, 300, "ZYNQ7010 PS：双 Cortex-A9 + DDR（512 MB）+ UART1", font(29, True), PS_LINE)

# ---------- cores ----------
box(110, 360, 430, 200, CPU_FILL, CPU_LINE)
text(130, 372, "CPU0 · Cortex-A9 #0", font(25, True))
text(130, 420,
     "② 私有 DDR：2 MB\n0x0010_0000 – 0x0030_0000\n入口 0x0010_0000\n职责：初始化、心跳、ping",
     font(18), INK)

box(1460, 360, 430, 200, CPU_FILL, CPU_LINE)
text(1480, 372, "CPU1 · Cortex-A9 #1", font(25, True))
text(1480, 420,
     "② 私有 DDR：2 MB\n0x0030_0000 – 0x0050_0000\n入口 0x0030_0000\n职责：等待初始化、心跳、pong",
     font(18), INK)

# ---------- shared block ----------
box(660, 340, 680, 250, SH_FILL, SH_LINE)
text(680, 350, "③ 共享 DDR 0x1000_0000（1 MB，Non-cacheable）", font(22, True), SH_LINE)

box(680, 392, 300, 178, SUB_FILL, SH_LINE, width=2, radius=10)
text(694, 400, "控制块", font(20, True))
text(694, 434,
     "magic / version\ninit_state\nuart_turn\nuart_want[2]\ncpu0_hb / cpu1_hb",
     font(17), INK, spacing=6)

box(1020, 392, 300, 178, SUB_FILL, SH_LINE, width=2, radius=10)
text(1034, 400, "命令信箱", font(20, True))
text(1034, 434,
     "seq / cmd / arg\npayload[64]\nack_seq / status\nresp_val\nresp（pong N）",
     font(17), INK, spacing=6)

# core <-> shared
arrow((540, 462), (660, 462), both=True)
text(548, 424, "心跳 / ping", font(17), GRAY)
arrow((1340, 462), (1460, 462), both=True)
text(1348, 424, "心跳 / pong", font(17), GRAY)

# ---------- UART ----------
box(660, 660, 680, 110, UART_FILL, UART_LINE)
text(680, 670, "④ PS UART1：0xE000_1000，115200-8N1", font(22, True), UART_LINE)
text(680, 712, "两核共用：Peterson 互斥 + 整行加锁发送", font(18), INK)

route([(325, 560), (325, 715), (660, 715)])
text(338, 618, "整行打印", font(17), GRAY)
route([(1675, 560), (1675, 715), (1340, 715)])
text(1545, 618, "整行打印", font(17), GRAY)

arrow((1000, 770), (1000, 906))

# ---------- external ----------
box(700, 906, 600, 82, EXT_FILL, EXT_LINE)
center(1000, 918, "USB-UART 桥 → PC COM7（115200-8N1）\n串口终端：观察心跳与 ping/pong",
       font(19))

# ---------- legend ----------
center(1000, 1012,
       "① 下载链路　② 私有 DDR 分区　③ 单一共享地址空间　④ 互斥共用串口",
       font(20, True), INK)
center(1000, 1046,
       "心跳：每核只写自己的 cpuX_hb、读对方计数；ping/pong：单生产者-单消费者信箱",
       font(18), GRAY)

img.save(OUT, "PNG")
print(OUT)
