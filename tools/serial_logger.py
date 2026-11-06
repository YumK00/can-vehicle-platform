#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serial_logger.py —— STM32 串口数据采集器  (v2)

解决两件事：
  1. 串口助手没有"保存到文件"功能 → 本脚本直接读串口，实时写 CSV
  2. 造数据集要手动标注 → 输入标签 + 回车，每行数据自动带上标签

用法：
  python serial_logger.py --list                      # 先看电脑上有哪些串口
  python serial_logger.py -p COM3                     # 默认 9600（跟 Serial_Init 一致）
  python serial_logger.py -p COM3 -b 115200 -o data.csv
  python serial_logger.py -p COM3 --single-key        # 单键模式（只在传统 cmd 窗口下可靠）

运行中（默认"行模式"，在任何终端都能用）：
  输入 1 / 2 / 3 / 4 再按回车   → 切换标签
  直接输入任意文字再按回车       → 把那段文字当标签（自定义）
  q 再回车                      → 退出

依赖：
  pip install pyserial
"""

import argparse
import csv
import os
import sys
import threading
import time
from datetime import datetime

# ---- Windows 控制台中文可能报编码错，先兜一下 ----
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

# ---- 依赖检查 ----
try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("[错误] 缺少 pyserial，请先安装：\n    pip install pyserial")
    sys.exit(1)

try:
    import msvcrt  # Windows 专用，仅 --single-key 模式用
    HAS_MSVCRT = True
except ImportError:
    msvcrt = None
    HAS_MSVCRT = False


# ============================================================
# 标签表：输入对应数字就切到这个名字
#   → 想加/改标签，改这里就行（它会成为 CSV 里 label 列的值）
# ============================================================
LABELS = {
    "1": "normal",        # 正常状态
    "2": "shade",         # 手遮住光敏
    "3": "torch",         # 手电筒强光
    "4": "ir_block",      # 挡住红外
}

# 串口里字段名 → CSV 列名（大小写不敏感，兼容几种常见写法）
KEY_ALIASES = {
    "t": "t_ms", "time": "t_ms", "tick": "t_ms", "ms": "t_ms", "t_ms": "t_ms",
    "ir": "ir", "hw": "ir", "hwcgq": "ir",
    "ldr": "ldr", "light": "ldr", "lux": "ldr", "gq": "ldr", "gqcgq": "ldr",
}

CSV_FIELDS = ["wall", "t_ms", "ir", "ldr", "label"]

STATUS_EVERY_SEC = 2.0      # 状态行刷新间隔


def list_serial_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("没有找到任何串口。检查 USB-TTL 是否插好、驱动是否安装。")
        return
    print("可用串口：")
    for p in ports:
        print(f"  {p.device:<8} {p.description}")


def parse_line(line: str):
    """把 't=12345,IR=3800,LDR=120' 解析成 {'t_ms':12345,'ir':3800,'ldr':120}"""
    out = {}
    for item in line.split(","):
        item = item.strip()
        if "=" not in item:
            continue
        k, _, v = item.partition("=")
        col = KEY_ALIASES.get(k.strip().lower())
        if col is None:
            continue
        try:
            out[col] = int(float(v.strip()))
        except ValueError:
            return None                      # 数值解析失败 → 整行丢掉
    return out if out else None


HINT_TEXT = """\
[提示] 收到 30 行但一行都解析不了。
  最可能的原因：STM32 还在发【裸二进制】而不是文本。
  把 serialSendTask 里的发送改成：
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "t=%lu,IR=%u,LDR=%u\\r\\n",
                     (unsigned long)xTaskGetTickCount(), d.hwcgq, d.gqcgq);
    Serial_SendData((uint8_t *)buf, (uint16_t)n, 500);
  别忘了 #include <stdio.h>"""


def build_parser():
    ap = argparse.ArgumentParser(
        description="STM32 串口数据采集器：读文本 → 存 CSV → 打标签",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("-p", "--port", help="串口号，例如 COM3")
    ap.add_argument("-b", "--baud", type=int, default=9600,
                    help="波特率，默认 9600（要和 Serial_Init() 里保持一致）")
    ap.add_argument("-o", "--out", help="输出文件名，默认自动按时间命名")
    ap.add_argument("--label", default="normal", help="初始标签，默认 normal")
    ap.add_argument("--single-key", action="store_true",
                    help="单键模式（不需要回车）。只在传统 cmd/PowerShell 窗口可靠，"
                         "VS Code 内置终端里通常无效")
    ap.add_argument("--list", action="store_true", help="列出可用串口后退出")
    return ap


def main():
    args = build_parser().parse_args()

    if args.list:
        list_serial_ports()
        return

    if not args.port:
        print("[错误] 没指定串口。先运行：python serial_logger.py --list\n")
        list_serial_ports()
        sys.exit(1)

    out_path = args.out or f"data_{datetime.now():%Y%m%d_%H%M%S}.csv"

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as e:
        print(f"[错误] 打不开串口 {args.port}：{e}")
        print("  → 检查：串口号对不对？是不是被串口助手占用了？驱动装了吗？")
        sys.exit(1)

    # ---------- 共享状态 ----------
    lock = threading.Lock()
    st = {"label": args.label, "quit": False}

    counts = {name: 0 for name in LABELS.values()}
    counts.setdefault(args.label, 0)

    def apply_label(new: str):
        with lock:
            st["label"] = new
        counts.setdefault(new, 0)
        # ★ 立刻打印醒目确认，解决"按了没反应"的错觉
        print(f"\n>>> 标签已切换 → 【{new}】 (该类已采 {counts[new]} 条)")
        print("标签> ", end="", flush=True)

    # ---------- 决定键盘模式 ----------
    kb_mode = None
    if args.single_key and HAS_MSVCRT:
        kb_mode = "single"
    elif args.single_key:
        print("[提示] 当前不是 Windows，--single-key 不可用，改用行模式。")

    if kb_mode is None:
        if sys.stdin.isatty():
            kb_mode = "line"
        else:
            print("[提示] 当前不是交互式终端，读不到键盘，全程用同一个标签。")

    print(f"串口 {args.port} @ {args.baud} 已打开")
    print(f"输出文件：{os.path.abspath(out_path)}")
    if kb_mode == "line":
        keys = "  ".join(f"{k}={v}" for k, v in LABELS.items())
        print(f"行模式（输入后按回车）：{keys}；q=退出")
        print("  也可以直接输入任意文字当标签，比如 abnormal_slow")
    elif kb_mode == "single":
        keys = "  ".join(f"{k}={v}" for k, v in LABELS.items())
        print(f"单键模式：按 {keys}，按 q 退出")
    print("-" * 70)

    # ---------- 行模式：后台线程读输入 ----------
    if kb_mode == "line":
        def kb_worker():
            while True:
                try:
                    line = input("标签> ")
                except (EOFError, KeyboardInterrupt):
                    with lock:
                        st["quit"] = True
                    return

                cmd = line.strip()
                if not cmd:
                    continue
                if cmd.lower() in ("q", "quit", "exit"):
                    with lock:
                        st["quit"] = True
                    return
                apply_label(LABELS.get(cmd, cmd))   # 数字查表，其它当自定义标签

        threading.Thread(target=kb_worker, daemon=True).start()

    # ---------- 主循环 ----------
    n_ok = n_bad = 0
    bad_shown = 0
    hint_shown = False
    last_status = 0.0

    try:
        with open(out_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            writer.writeheader()

            while True:
                # 1) 取当前标签 / 检查退出
                with lock:
                    label = st["label"]
                    if st["quit"]:
                        break

                # 2) 单键模式在主循环里轮询
                if kb_mode == "single":
                    while msvcrt.kbhit():
                        ch = msvcrt.getch().decode(errors="ignore").lower()
                        if ch == "q":
                            with lock:
                                st["quit"] = True
                            break
                        if ch in LABELS:
                            apply_label(LABELS[ch])

                # 3) 读一行串口数据
                try:
                    raw = ser.readline()
                except serial.SerialException as e:
                    print(f"\n[错误] 串口读取失败：{e}")
                    break

                if not raw:
                    continue
                line = raw.decode(errors="ignore").strip()
                if not line:
                    continue

                data = parse_line(line)
                if data is None:
                    n_bad += 1
                    if bad_shown < 5:
                        print(f"\n[跳过] 无法解析：{line!r}")
                        bad_shown += 1
                    if n_ok == 0 and n_bad == 30 and not hint_shown:
                        hint_shown = True
                        print("\n" + HINT_TEXT)
                    continue

                # 4) 写 CSV（边写边落盘，拔线不丢）
                row = {
                    "wall": f"{datetime.now():%H:%M:%S.%f}"[:-3],
                    "t_ms": data.get("t_ms", ""),
                    "ir": data.get("ir", ""),
                    "ldr": data.get("ldr", ""),
                    "label": label,
                }
                writer.writerow(row)
                f.flush()
                n_ok += 1
                counts[label] = counts.get(label, 0) + 1

                # 5) 状态行（每 2 秒一次；不用 \r，免得和键盘输入打架）
                now = time.time()
                if now - last_status > STATUS_EVERY_SEC:
                    last_status = now
                    print(f"  [{label}] 已采 {n_ok} 条 | IR={row['ir']} "
                          f"LDR={row['ldr']} | 跳过 {n_bad}")

    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

    # ---------- 收尾统计 ----------
    print("\n" + "-" * 70)
    print(f"结束。共写入 {n_ok} 行，跳过 {n_bad} 行")
    print("各标签样本数：")
    for name, c in sorted(counts.items(), key=lambda x: -x[1]):
        if c:
            print(f"    {name:<16} {c}")
    print(f"文件：{os.path.abspath(out_path)}")

    nonzero = [k for k, v in counts.items() if v]
    if n_ok and nonzero == ["normal"]:
        print("\n[提示] 全是 normal。要用有监督模型就再采几段异常；")
        print("       走自编码器 / 3σ 路线的话，这样就够了。")


if __name__ == "__main__":
    main()
