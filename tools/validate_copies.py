#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
validate_copies.py —— 校验各工程里的协议副本是否与单一真源一致。

这是"契约治理"的护栏：任何人只改某一个工程里的 protocol.h / contract 头文件，
这里就会报出来（配合 CI 可以拦住协议分叉）。

用法：
  python tools/validate_copies.py
退出码 0 = 全部一致。
"""

import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

COPIES = [
    ("protocol/protocol.h", "firmware/gateway/component/MiddleWare/PROTOCOL/protocol.h", "网关 C 端 protocol.h"),
    ("protocol/protocol.h", "firmware/collect-node/Core/Inc/protocol.h", "采集节点 protocol.h"),
    ("protocol/protocol.h", "firmware/control-node/Core/Inc/protocol.h", "执行节点 protocol.h"),
    ("protocol/generated/contract_c.h", "firmware/gateway/component/MiddleWare/PROTOCOL/contract_c.h", "网关 C 契约头"),
    ("protocol/generated/contract_qt.h", "host/desktop-console/contract_qt.h", "桌面端 Qt 契约头"),
    ("protocol/generated/contract_qt.h", "host/mobile-app/contract_qt.h", "手机端 Qt 契约头"),
]


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()[:16]


def main():
    print("单一真源 SHA256 前缀：")
    sources = {}
    for src, _dst, _label in COPIES:
        s = ROOT / src
        if src not in sources and s.exists():
            sources[src] = digest(s)
            print("  %-38s %s" % (s.name, sources[src]))
    print()

    bad = 0
    for src, dst, label in COPIES:
        s, d = ROOT / src, ROOT / dst
        if not s.exists():
            print("[FAIL] 真源缺失:", src)
            bad += 1
            continue
        if not d.exists():
            print("[FAIL] 副本缺失:", dst, "→ 跑 tools/sync_contract_copies.py")
            bad += 1
            continue
        if s.read_bytes() != d.read_bytes():
            print("[DRIFT] %s\n         真源 %s (%s)\n         副本 %s (%s)"
                  % (label, src, digest(s), dst, digest(d)))
            bad += 1
        else:
            print("[OK]   %-16s %s" % (label, dst))

    print("\n%s" % ("全部一致 ✔" if bad == 0 else "发现 %d 处不一致 ✘" % bad))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
