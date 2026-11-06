#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sync_contract_copies.py —— 把单一真源同步到各工程副本（消除多端协议分叉）。

同步内容：
  protocol/protocol.h              → firmware/{gateway,collect-node,control-node}
  protocol/generated/contract_c.h  → firmware/gateway（C 端契约常量）
  protocol/generated/contract_qt.h → host/{desktop-console,mobile-app}（Qt 端契约常量）

用法：
  python tools/sync_contract_copies.py          # 同步
  python tools/sync_contract_copies.py --dry    # 只看会改哪些文件
"""

import argparse
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

COPIES = [
    ("protocol/protocol.h", "firmware/gateway/component/MiddleWare/PROTOCOL/protocol.h"),
    ("protocol/protocol.h", "firmware/collect-node/Core/Inc/protocol.h"),
    ("protocol/protocol.h", "firmware/control-node/Core/Inc/protocol.h"),
    ("protocol/generated/contract_c.h", "firmware/gateway/component/MiddleWare/PROTOCOL/contract_c.h"),
    ("protocol/generated/contract_qt.h", "host/desktop-console/contract_qt.h"),
    ("protocol/generated/contract_qt.h", "host/mobile-app/contract_qt.h"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true")
    args = ap.parse_args()

    changed = 0
    for src, dst in COPIES:
        s, d = ROOT / src, ROOT / dst
        if not s.exists():
            print("[MISS] 源文件不存在:", src)
            return 1
        same = d.exists() and d.read_bytes() == s.read_bytes()
        if same:
            print("[same]", dst)
            continue
        print("[sync]", dst)
        changed += 1
        if not args.dry:
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(s, d)
    print("\n%s %d 个文件" % ("将更新" if args.dry else "已更新", changed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
