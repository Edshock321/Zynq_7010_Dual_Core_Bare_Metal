"""Build the ZYNQ7010 dual-core AMP summary DOCX."""

from pathlib import Path

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_ALIGN_VERTICAL, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "ZYNQ7010双核AMP方案总结.docx"
IMG = ROOT / "amp_arch_diagram.png"

EA_FONT = "微软雅黑"
MONO = "Consolas"
NAVY = "1F4E79"
BLUE = "2E75B6"
GRAY = "595959"

doc = Document()

# ---------- page setup ----------
sec = doc.sections[0]
sec.page_width = Cm(21.0)
sec.page_height = Cm(29.7)
sec.top_margin = Cm(2.2)
sec.bottom_margin = Cm(2.0)
sec.left_margin = Cm(2.4)
sec.right_margin = Cm(2.0)
CONTENT_W = 16.6


def set_run(run, size=10.5, bold=False, color=None, font=EA_FONT, italic=False):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    if color:
        run.font.color.rgb = RGBColor.from_string(color)
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.find(qn("w:rFonts"))
    if rFonts is None:
        rFonts = OxmlElement("w:rFonts")
        rPr.append(rFonts)
    rFonts.set(qn("w:ascii"), font)
    rFonts.set(qn("w:hAnsi"), font)
    rFonts.set(qn("w:eastAsia"), font)


def body(text, size=10.5, bold=False, color=None, align=None, after=4, before=0,
         indent=0.0, line=1.35, keep_next=False):
    p = doc.add_paragraph()
    if align is not None:
        p.alignment = align
    pf = p.paragraph_format
    pf.space_before = Pt(before)
    pf.space_after = Pt(after)
    pf.line_spacing = line
    pf.keep_with_next = keep_next
    if indent:
        pf.first_line_indent = Cm(indent)
    r = p.add_run(text)
    set_run(r, size, bold, color)
    return p


def bullet(text, size=10.5):
    p = doc.add_paragraph(style="List Bullet")
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.line_spacing = 1.3
    r = p.add_run(text)
    set_run(r, size)
    return p


def h1(text, page_break=False):
    p = doc.add_heading(level=1)
    p.paragraph_format.space_before = Pt(14)
    p.paragraph_format.space_after = Pt(6)
    p.paragraph_format.keep_with_next = True
    p.paragraph_format.page_break_before = page_break
    r = p.add_run(text)
    set_run(r, 16, True, NAVY)
    # thin bottom rule
    pPr = p._p.get_or_add_pPr()
    pBdr = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), "6")
    bottom.set(qn("w:space"), "2")
    bottom.set(qn("w:color"), "BDD7EE")
    pBdr.append(bottom)
    pPr.append(pBdr)
    return p


def h2(text):
    p = doc.add_heading(level=2)
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(4)
    p.paragraph_format.keep_with_next = True
    r = p.add_run(text)
    set_run(r, 13, True, BLUE)
    return p


def shade_paragraph(p, fill="F2F6FA"):
    pPr = p._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    pPr.append(shd)


def shade_cell(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tcPr.append(shd)


def set_cell_text(cell, text, size=9.5, bold=False, color=None, align=None):
    cell.text = ""
    p = cell.paragraphs[0]
    if align is not None:
        p.alignment = align
    p.paragraph_format.space_before = Pt(1)
    p.paragraph_format.space_after = Pt(1)
    p.paragraph_format.line_spacing = 1.12
    r = p.add_run(text)
    set_run(r, size, bold, color)
    cell.vertical_alignment = WD_ALIGN_VERTICAL.CENTER


def add_table(headers, rows, widths, header_fill=NAVY, size=9.5):
    t = doc.add_table(rows=1, cols=len(headers))
    t.style = "Table Grid"
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    t.autofit = False

    hdr = t.rows[0]
    trPr = hdr._tr.get_or_add_trPr()
    tblHeader = OxmlElement("w:tblHeader")
    trPr.append(tblHeader)

    for i, h in enumerate(headers):
        c = hdr.cells[i]
        set_cell_text(c, h, size=size, bold=True, color="FFFFFF",
                      align=WD_ALIGN_PARAGRAPH.CENTER)
        shade_cell(c, header_fill)

    for row in rows:
        cells = t.add_row().cells
        for i, val in enumerate(row):
            set_cell_text(cells[i], str(val), size=size)

    for row in t.rows:
        for i, w in enumerate(widths):
            row.cells[i].width = Cm(w)
        trPr = row._tr.get_or_add_trPr()
        trPr.append(OxmlElement("w:cantSplit"))
    return t


def add_code(text):
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.left_indent = Cm(0.4)
    pf.space_before = Pt(4)
    pf.space_after = Pt(8)
    pf.line_spacing = 1.0
    pf.keep_together = True
    pPr = p._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), "F7F7F7")
    pPr.append(shd)
    pBdr = OxmlElement("w:pBdr")
    for side in ("top", "left", "bottom", "right"):
        el = OxmlElement(f"w:{side}")
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), "4")
        el.set(qn("w:space"), "4")
        el.set(qn("w:color"), "D9D9D9")
        pBdr.append(el)
    pPr.append(pBdr)
    r = p.add_run(text)
    set_run(r, 8.5, False, "262626", font=MONO)
    return p


def caption(text):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(2)
    p.paragraph_format.space_after = Pt(8)
    r = p.add_run(text)
    set_run(r, 9, False, GRAY)
    return p


def footer_page_number():
    footer = sec.footer
    p = footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r1 = p.add_run("ZYNQ7010 双核 AMP 验证总结　·　第 ")
    set_run(r1, 8.5, False, "808080")
    r2 = p.add_run()
    set_run(r2, 8.5, False, "808080")
    fld1 = OxmlElement("w:fldChar")
    fld1.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = "PAGE"
    fld2 = OxmlElement("w:fldChar")
    fld2.set(qn("w:fldCharType"), "end")
    r2._r.append(fld1)
    r2._r.append(instr)
    r2._r.append(fld2)
    r3 = p.add_run(" 页")
    set_run(r3, 8.5, False, "808080")


# ================= cover / title =================
p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_before = Pt(10)
p.paragraph_format.space_after = Pt(2)
r = p.add_run("技术总结")
set_run(r, 11, False, GRAY)

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(4)
r = p.add_run("ZYNQ7010 双核 AMP 验证总结")
set_run(r, 24, True, NAVY)

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(10)
r = p.add_run("Vitis 2024.1　·　XC7Z010　·　双 Cortex-A9 裸机 AMP")
set_run(r, 12, False, BLUE)

meta = add_table(
    ["文档版本", "日期", "适用平台", "验证状态"],
    [["V1.0", "2026-11-15", "XC7Z010 / Vitis 2024.1", "已上板验证（45 s）"]],
    [2.6, 3.0, 5.4, 5.6],
    header_fill="DEEBF7",
    size=9.5,
)
# header style of meta table: dark text on light fill
for c in meta.rows[0].cells:
    for pp in c.paragraphs:
        for rr in pp.runs:
            rr.font.color.rgb = RGBColor.from_string(NAVY)
            rr.font.bold = True

# ================= abstract =================
h1("摘要")
p = body(
    "本方案在 XC7Z010 上实现双 Cortex-A9 裸机 AMP（非对称多处理）：CPU0 与 CPU1 各自运行在"
    "独立的 2 MB DDR 段，通过 0x10000000 起的 1 MB 非缓存共享区交换心跳计数与命令数据；"
    "两核共用 PS UART1（115200-8N1），以 Peterson 算法互斥后整行输出。"
    "已在板卡上连续运行 45 秒：串口输出 181 行，坏行 0、告警 0，23 组 ping/pong 序号完全对应，"
    "验证了并行运行、打印互斥、私有内存隔离与共享数据控制四项设计目标。",
    after=6,
)
shade_paragraph(p)

# ================= 1 =================
h1("一、项目概述")
body(
    "验证目标是在 ZYNQ7010 上建立一套可复用的双核裸机骨架，具体包括：",
    after=2,
)
bullet("两个 CPU 核同时运行：CPU0、CPU1 各自有独立入口与栈，互不覆盖；")
bullet("两核都通过 PS UART1、115200-8N1 打印心跳，并保证整行输出不交错；")
bullet("两核的代码、数据、堆栈、MMU 表分处不同 DDR 段，互不重叠；")
bullet("只用一个共享地址空间完成心跳状态与命令数据交换，不依赖额外外设。")
body(
    "软件环境为 Vitis 2024.1：platform 提供 standalone BSP（两个 domain 分别绑定 A9 #0/#1），"
    "FSBL 负责 PS/DDR 初始化，两个应用分别加载到两个核。板卡 DDR 512 MB，"
    "PS UART1 经 USB-UART 桥接到 PC 的 COM7。",
    before=2,
)

# ================= 2 =================
h1("二、工程划分", page_break=True)
body(
    "工作空间按“平台 / 启动 / 两个应用 / 系统聚合 / 公共代码”划分，职责边界清晰：",
    after=4,
)
add_table(
    ["工程或文件", "类型", "绑定 / 位置", "职责"],
    [
        ["platform", "平台工程", "standalone_ps7_cortexa9_0、core2", "BSP、硬件参数、导出 platform.xpfm；两个 domain 分别给两个核"],
        ["platform/zynq_fsbl", "FSBL", "CPU0", "上电/调试时初始化 PS、DDR、时钟、UART，并交出控制权"],
        ["hello_world", "应用", "ps7_cortexa9_0（A9 #0）", "复位并初始化共享区、配置 UART、打印心跳、发起 ping"],
        ["hello_world_1", "应用", "ps7_cortexa9_1（A9 #1）", "等待 CPU0 初始化完成、打印心跳、应答 pong"],
        ["amp_system", "System 工程", "聚合上述两个应用", "一个调试配置同时下载两核，避免分别启动时互相复位"],
        ["amp_common.h", "公共头文件", "工作空间根目录", "共享地址、控制块、Peterson 互斥、UART、延时、信箱协议"],
        ["run_amp.tcl", "下载脚本", "工作空间根目录", "xsct 一键复位 PS、下比特流、ps7_init、装两个 ELF、运行"],
        ["build_amp.ps1", "编译脚本", "工作空间根目录", "命令行增量编译两个应用"],
    ],
    [4.1, 2.4, 4.6, 5.5],
)
body(
    "两个应用源码目录下的 amp_common.h 只是四行转发头（#include \"../../amp_common.h\"），"
    "实际定义只有根目录一份，编译依赖会记录到根文件，不存在两份副本不同步的问题。",
    before=4, size=9.5, color=GRAY,
)

# ================= 3 =================
h1("三、内存划分")
body(
    "三块区域按 2 MB / 2 MB / 1 MB 划分，共享区起点在 1 MB 边界，正好对应一个 MMU 段：",
    after=4,
)
add_table(
    ["区域", "地址范围", "大小", "用途与属性"],
    [
        ["CPU0 私有", "0x0010_0000 – 0x0030_0000", "2 MB", "代码、只读数据、data、bss、MMU 表、堆、栈；入口 0x0010_0000"],
        ["CPU1 私有", "0x0030_0000 – 0x0050_0000", "2 MB", "同上；入口 0x0030_0000，栈顶约 0x0030_D830"],
        ["两核共享", "0x1000_0000 – 0x100F_FFFF", "1 MB", "控制块 + 命令信箱；Normal Non-cacheable + shareable"],
    ],
    [2.6, 5.4, 1.8, 6.8],
)
body(
    "共享区在 0x1000_0000 这一 1 MB 边界上，因此 Xil_SetTlbAttributes() 一次调用即可把整块设为"
    " NORM_NONCACHE；两核访问的是同一份物理内存，普通读写不需要 cache 维护。"
    "私有段仍保持 D-Cache 开启，只有共享段关缓存。",
    before=4,
)

# ================= 4 =================
h1("四、双核运行方案")
h2("4.1 启动与下载")
body(
    "调试主机通过 JTAG 连接 xc7z010，依次完成：下载比特流 → 运行 ps7_init.tcl（或 FSBL）完成"
    " PS/DDR/时钟/UART 初始化 → 把 hello_world.elf 装入 A9 #0、hello_world_1.elf 装入 A9 #1 →"
    "两个核分别 continue。Vitis 的 System 配置和命令行 run_amp.tcl 都是这个流程。",
    after=4,
)
body(
    "共享区初始化固定由 CPU0 负责：CPU0 每次启动都先把 init_state 置 0、清空控制块、配置 UART，"
    "最后写 version/magic 并把 init_state 置为 DONE；CPU1 只轮询等待 DONE。"
    "早先“两核抢初始化权”的共享 CAS 方案在复位后 DDR 残留 BUSY、或两核同时 CAS 时，会双双卡在等待里，"
    "固定职责后行为确定、可重复下载。",
    after=4,
)

h2("4.2 共享控制块")
body(
    "控制块与命令信箱合并在同一个 0xC8 字节结构体中，两核按同一份布局访问；"
    "所有字段 4 字节对齐，单字 32 位读写不会撕裂：",
    after=4,
)
add_table(
    ["偏移", "字段", "方向", "作用"],
    [
        ["0x00", "magic", "CPU0 写", "0x414D5032（\"AMP2\"），共享区有效标志"],
        ["0x04", "version", "CPU0 写", "0x00020200，布局版本"],
        ["0x08", "init_state", "CPU0 写", "0=未就绪，0x444F4E45（\"DONE\"）=可用"],
        ["0x0C", "uart_turn", "两核", "Peterson：当前轮到谁打印"],
        ["0x10", "uart_want[0]", "CPU0 写", "CPU0 想打印"],
        ["0x14", "uart_want[1]", "CPU1 写", "CPU1 想打印"],
        ["0x18", "lock_recoveries", "两核", "等待超时强制接管次数"],
        ["0x1C", "cpu1_ready", "CPU1 写", "CPU1 已进入主循环"],
        ["0x20", "cpu0_hb", "CPU0 写", "CPU0 心跳计数"],
        ["0x24", "cpu1_hb", "CPU1 写", "CPU1 心跳计数"],
        ["0x28", "seq", "CPU0 写", "命令序号，0=无命令"],
        ["0x2C", "cmd", "CPU0 写", "AMP_CMD_PING"],
        ["0x30", "arg", "CPU0 写", "命令参数（= 序号）"],
        ["0x34", "payload_len", "CPU0 写", "payload 有效字节数"],
        ["0x38", "payload[64]", "CPU0 写", "命令附带数据（\"ping\"）"],
        ["0x78", "ack_seq", "CPU1 写", "已处理完的命令序号"],
        ["0x7C", "status", "CPU1 写", "0=OK，1=不支持的命令"],
        ["0x80", "resp_val", "CPU1 写", "应答附带数据（CPU1 当前心跳）"],
        ["0x84", "resp_len", "CPU1 写", "resp 字节数"],
        ["0x88", "resp[64]", "CPU1 写", "应答字符串（\"pong N\"）"],
    ],
    [1.5, 3.2, 2.2, 9.3],
    size=9.0,
)

h2("4.3 UART 互斥")
body(
    "两核的打印都封装为“整行”操作：先把前缀、数字、后缀格式化到本地缓冲区，再一次取锁、连续发送、释放锁。"
    "锁的粒度因此是整行而不是单个字符，另一核不会插进一行中间。",
    after=4,
)
body(
    "锁的实现没有使用 LDREX/STREX。板上实测发现：两个 A9 同时对同一个 Normal Non-cacheable 地址做"
    "独占访问，会不断清掉对方的独占监视器，双方一起卡在 STREX 重试环里——调试器读回两个核的 PC 都在"
    " STREX 之后，锁值被失败尝试写成 1 却没有任何核真正持有。这是 LL/SC 活锁，加只读退避或超时都不能根治。"
    "最终改用只依赖普通读写的 Peterson 双进程算法：",
    after=4,
)
add_code(
    "进入区：want[me] = 1;  turn = other;  dmb;\n"
    "等待区：while (want[other] && turn == other) ;\n"
    "临界区：整行发送（一次持锁）\n"
    "退出区：want[me] = 0;"
)
body(
    "共享区是非缓存且 shareable 的，普通 load/store 直接落到物理内存，配合 dmb 即可保证顺序；"
    "不需要独占访问，因此没有活锁。两核启动顺序无关；单核运行时对方 want=0，可直接进入。"
    "若等待超过约 2000 万次（秒级），会打印 "
    "“[AMP] WARN: uart mutex timeout, forced takeover” 并清掉对方意愿标志接管，"
    "用于某核被调试器停住时保住本核输出（尽力恢复）。",
    after=4,
)

h2("4.4 命令信箱")
body(
    "信箱是单生产者/单消费者模型：CPU0 只写命令侧，CPU1 只写应答侧，同一时刻只有一条命令在途，"
    "因此不需要额外加锁，只用 acquire/release 语义保证顺序：",
    after=4,
)
add_code(
    "CPU0：ack_seq == seq-1 ?\n"
    "        写 payload / cmd / arg → seq = N（release）\n"
    "CPU1：seq != 0 ?\n"
    "        读 cmd / arg / payload → 打印 recv → 写 resp / status → seq = 0 → ack_seq = N\n"
    "CPU0：等待 ack_seq == N（500 ms 超时）→ 读 resp → 打印 ping #N -> pong N"
)
body(
    "先清 seq 再写 ack_seq，保证 CPU0 看到 ack 时 CPU1 一定已读完旧命令，不会覆盖未读完的数据；"
    "超时不会强行发下一条，等 CPU1 恢复应答后自动继续。",
    after=4,
)

# ================= 5 =================
h1("五、方案示意图及说明")
doc.add_picture(str(IMG), width=Cm(16.4))
doc.paragraphs[-1].alignment = WD_ALIGN_PARAGRAPH.CENTER
caption("图 1　ZYNQ7010 双核 AMP 方案示意图（① 下载链路　② 私有 DDR 分区　③ 共享地址空间　④ UART 互斥共用）")

body("图中四个编号对应四条主线，逐项说明如下：", after=4)

body(
    "① 下载链路：开发主机（Vitis IDE 或 xsct）经 Digilent JTAG-SMT2 连接 xc7z010。"
    "先下载 PL 比特流，再用 ps7_init.tcl 完成 PS、DDR、时钟和 UART1 初始化，"
    "最后把两个 ELF 分别装入 A9 #0 与 A9 #1 并启动。下载过程会复位 PS，DDR 原有内容丢失。",
    after=4,
)
body(
    "② 私有 DDR 分区：CPU0 使用 0x0010_0000–0x0030_0000，CPU1 使用 0x0030_0000–0x0050_0000，"
    "各 2 MB。代码、只读数据、data/bss、MMU 表、堆和栈都在各自段内，入口地址分别是 0x0010_0000 与"
    " 0x0030_0000。两段首尾相接但不重叠，任何一核的栈溢出都不会踩到对方。",
    after=4,
)
body(
    "③ 单一共享地址空间：0x1000_0000 起 1 MB，映射为 Normal Non-cacheable + shareable。"
    "左侧控制块放初始化标志、Peterson 互斥变量和两核心跳计数；右侧命令信箱放序号、命令、参数、应答与"
    "两段 64 字节数据区。两核读写同一份结构体，靠 32 位对齐访问避免撕裂。",
    after=4,
)
body(
    "④ 互斥共用串口：两核的所有打印都先经过同一把 Peterson 锁，再整行写入 UART1 FIFO。"
    "UART1 基地址 0xE000_1000，115200-8N1，经 USB-UART 桥接到 PC 的 COM7。"
    "锁只保护 UART 临界区，不参与心跳和信箱的数据路径，因此通信与打印互不阻塞。",
    after=4,
)
body(
    "把四条主线串起来看：心跳是“各写各的、互相读”——CPU0 写 cpu0_hb、读 cpu1_hb，CPU1 反之；"
    "ping/pong 是“CPU0 写命令、CPU1 写应答”的单向流水；两条路径都不加锁，只有真正写串口时才竞争 Peterson 锁。"
    "这样既保证了共享数据的实时可见，又把互斥范围压到最小。",
    after=4,
)

# ================= 6 =================
h1("六、实测结果")
body(
    "使用 run_amp.tcl 复位下载后，在 COM7 上连续抓取 45 秒（原始日志 amp_serial_capture.log）：",
    after=4,
)
add_table(
    ["指标", "结果"],
    [
        ["抓取时长 / 串口", "45 s / COM7，115200-8N1"],
        ["非空行数", "181"],
        ["坏行（不符合 [CPUx] 行格式）", "0"],
        ["WARN / timeout / forced", "0"],
        ["CPU0 心跳", "45 条，序号 26→70 严格递增（1 s/条）"],
        ["CPU1 心跳", "90 条，序号 50→139 严格递增（0.5 s/条）"],
        ["ping/pong", "23 组，序号 13→35 连续，ping #N 与 pong N 全部一致"],
        ["recv 与 ping 对应", "23/23 一一对应"],
        ["行内夹带第二个 [CPU 标记", "0（整行互斥有效）"],
        ["RTT", "约 8.9–9.4 ms（含 CPU1 的 recv 打印与 10 ms 轮询周期）"],
    ],
    [6.4, 10.2],
)
body("实际输出节选：", before=4, after=2, keep_next=True)
add_code(
    "[CPU0] heartbeat #1  (cpu1_hb=0)\n"
    "[CPU1] heartbeat #1  (cpu0_hb=1)\n"
    "[CPU1] heartbeat #2  (cpu0_hb=1)\n"
    "[CPU0] heartbeat #2  (cpu1_hb=2)\n"
    "[CPU1] recv #1: ping\n"
    "[CPU0] ping #1 -> pong 1 (cpu1_hb=2, rtt=4066 us)\n"
    "...\n"
    "[CPU1] heartbeat #94  (cpu0_hb=47)\n"
    "[CPU0] heartbeat #48  (cpu1_hb=94)\n"
    "[CPU1] recv #24: ping\n"
    "[CPU0] ping #24 -> pong 24 (cpu1_hb=94, rtt=9063 us)"
)

# ================= 7 =================
h1("七、结论与注意事项")
body("三点结论：", after=2, keep_next=True)
bullet("双核并行、UART 互斥、私有内存隔离、共享数据控制四项设计目标均已通过上板验证；")
bullet("方案层规避了两个硬件相关风险：A9 双核 LDREX/STREX 活锁（改用 Peterson）、"
       "共享区初始化竞争（固定 CPU0 初始化、每次启动重置）；")
bullet("工程结构可以直接作为后续 AMP 业务的骨架：把 CPU1 的心跳循环替换为算法/采集任务，"
       "CPU0 保留控制与通信职责即可。")
body("使用时注意：", before=6, after=2, keep_next=True)
bullet("COM7 是独占端口：串口助手打开时，脚本或其它程序无法读取；抓日志前先关闭助手。")
bullet("下载会复位 PS 并重下比特流，板上原有程序与 DDR 内容都会丢失，属预期行为。")
bullet("调试流程总是先启动 CPU0：CPU1 只等待 DONE，单独启动 CPU1 会停在等待并打印一次提示。")
bullet("出现 “[AMP] WARN: uart mutex timeout, forced takeover” 说明另一核被停住超过秒级；"
       "正常运行（本次 45 秒）不应出现。")
bullet("移植到其它板卡时，需要同步修改 UART 实例/时钟、DDR 容量、共享区地址与两个链接脚本的 DDR 段。")

# ================= 8 =================
h1("八、文件清单", page_break=True)
add_table(
    ["文件", "说明"],
    [
        ["amp_common.h", "公共定义：地址、共享块、Peterson 互斥、UART、延时、格式化输出、信箱协议"],
        ["hello_world/src/helloworld.c", "CPU0：初始化共享区、心跳、发起 ping、打印 pong"],
        ["hello_world_1/src/helloworld.c", "CPU1：等待初始化、心跳、应答 pong"],
        ["run_amp.tcl", "xsct 复位/下载/运行脚本"],
        ["build_amp.ps1", "命令行增量编译脚本"],
        ["amp_arch_diagram.png", "本方案示意图（图 1 源图）"],
        ["AMP双核裸跑测试说明.md", "完整构建、下载、排障与验证清单"],
        ["amp_serial_capture.log", "45 秒实测串口日志"],
    ],
    [7.0, 9.6],
)

footer_page_number()
doc.save(OUT)
print(OUT)
