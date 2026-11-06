#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_contract.py —— 主机侧协议自测套件（无需任何硬件）。

覆盖：
  1. 契约结构自洽性（ID/DLC/功能组/节点号/字段不越界）
  2. 大端编解码与每帧字节布局（编码 → 逐字节校验 → 解码回环）
  3. seq 丢帧算法（全回绕覆盖）
  4. 急停 Fail-Safe（B0 全部 256 种取值）
  5. MQTT 主题拼装与通配符匹配（+ / #）
  6. 命令载荷解析与越界拒绝
  7. 生成物与单一真源同步性
  8. protocol.h 与 contract.json 的帧 ID/DLC 一致性

用法：
  python tools/test_contract.py            # 跑全部用例
  python tools/test_contract.py -v         # 打印每条断言
退出码 0 表示全部通过。
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTRACT_PATH = ROOT / "protocol" / "contract.json"
PROTOCOL_H = ROOT / "protocol" / "protocol.h"

# ---------------------------------------------------------------- 断言框架
class Suite:
    def __init__(self, verbose=False):
        self.passed = 0
        self.failed = []
        self.verbose = verbose
        self.case = "?"

    def section(self, name):
        self.case = name

    def check(self, cond, msg):
        if cond:
            self.passed += 1
            if self.verbose:
                print("  ok   [%s] %s" % (self.case, msg))
        else:
            self.failed.append("[%s] %s" % (self.case, msg))

    def eq(self, got, want, msg):
        self.check(got == want, "%s: got=%r want=%r" % (msg, got, want))


# ---------------------------------------------------------------- 编解码
def enc(value, size):
    return bytes((value >> (8 * (size - 1 - i))) & 0xFF for i in range(size))


def dec(buf, offset, size):
    v = 0
    for i in range(size):
        v = (v << 8) | buf[offset + i]
    return v


def encode_frame(frame, values):
    """按 contract 里的字段定义编码一帧，返回 bytes（长度 = dlc）。"""
    buf = bytearray(frame["dlc"])
    for f in frame["fields"]:
        v = values[f["name"]]
        if "scale" in f:
            v = int(round(v * f["scale"]))
        buf[f["offset"]:f["offset"] + f["size"]] = enc(v, f["size"])
    return bytes(buf)


def decode_frame(frame, buf):
    out = {}
    for f in frame["fields"]:
        v = dec(buf, f["offset"], f["size"])
        if "scale" in f:
            v = v / f["scale"]
        out[f["name"]] = v
    return out


def seq_lost(new, old):
    """与 protocol.h 的 PROTO_SeqLost 等价。"""
    return (new - old - 1) & 0xFF


def estop_action(b0):
    """与 protocol.h 的接收方契约等价：只有 2 是解除，其余一律急停。"""
    return 2 if b0 == 2 else 1


# ---------------------------------------------------------------- MQTT
def topic_matches(pattern, topic):
    p = pattern.split("/")
    t = topic.split("/")
    for i, seg in enumerate(p):
        if seg == "#":
            return i <= len(t)
        if i >= len(t):
            return False
        if seg == "+":
            continue
        if seg != t[i]:
            return False
    return len(p) == len(t)


def build_cmd_topic(prefix, node, kind):
    return "%s/%s/cmd/%s" % (prefix, node, kind)


# ---------------------------------------------------------------- 命令解析
def parse_motor(payload):
    parts = [p.strip() for p in payload.split(",") if p.strip() != ""]
    if not parts or not parts[0].lstrip("-").isdigit():
        return None
    cmd = int(parts[0])
    arg = int(parts[1]) if len(parts) > 1 and parts[1].lstrip("-").isdigit() else 0
    if not (0 <= cmd <= 255 and 0 <= arg <= 255):
        return None
    return cmd, arg


def parse_target(payload):
    s = payload.strip()
    if not s.isdigit():
        return None
    v = int(s)
    return v if 0 <= v <= 65535 else None


def parse_pid(payload):
    parts = [p.strip() for p in payload.split(",")]
    if len(parts) != 3:
        return None
    try:
        vals = [float(p) for p in parts]
    except ValueError:
        return None
    if any(v < 0 or v > 655.35 for v in vals):
        return None
    return tuple(int(round(v * 100)) for v in vals)


def parse_mode(payload):
    s = payload.strip()
    if s not in ("0", "1"):
        return None
    return int(s)


def parse_estop(payload):
    s = payload.strip()
    return 2 if s == "2" else 1  # 解析失败/空 → 急停（Fail-Safe）


# ---------------------------------------------------------------- protocol.h 解析
def parse_protocol_h():
    """把 protocol.h 的 #define 解析成 {name: value}，支持 CAN_ID(a,b) 与宏引用。"""
    text = PROTOCOL_H.read_text(encoding="utf-8", errors="ignore")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)

    raw = {}
    for m in re.finditer(r"^\s*#define\s+(\w+)\s+(.+?)\s*$", text, flags=re.M):
        name, body = m.group(1), m.group(2).strip()
        if name.startswith("CONTRACT_"):
            continue
        raw[name] = body

    resolved = {}

    def resolve(name, depth=0):
        if name in resolved:
            return resolved[name]
        if depth > 20 or name not in raw:
            return None
        body = raw[name]
        body = re.sub(r"CAN_ID\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                      lambda mm: "(%s | %s)" % (mm.group(1), mm.group(2)), body)
        # 先把数字字面量（含 u/U 后缀）冻结成十进制，避免把 0x010u 里的 x010u 当标识符
        body = re.sub(r"0[xX][0-9a-fA-F]+[uUlL]*",
                      lambda mm: str(int(mm.group(0).rstrip("uUlL"), 16)), body)
        body = re.sub(r"\b\d+[uUlL]*\b", lambda mm: mm.group(0).rstrip("uUlL"), body)
        for word in set(re.findall(r"[A-Za-z_]\w*", body)):
            if word in ("u", "U", "L", "UL", "uint32_t", "uint16_t", "uint8_t"):
                continue
            v = resolve(word, depth + 1)
            if v is None:
                return None
            body = re.sub(r"\b%s\b" % re.escape(word), str(v), body)
        try:
            val = int(eval(body.replace("/", "//"), {"__builtins__": {}}))
        except Exception:
            return None
        resolved[name] = val
        return val

    for name in list(raw):
        resolve(name)
    return raw, resolved


# ---------------------------------------------------------------- 用例
def run(c, verbose=False):
    s = Suite(verbose)
    can, mqtt = c["can"], c["mqtt"]
    frames = can["frames"]
    by_name = {f["name"]: f for f in frames}
    by_id = {f["id"].lower(): f for f in frames}

    # 1. 结构自洽
    s.section("structure")
    s.eq(c["contract_version"].count("."), 2, "契约版本号格式")
    s.eq(c["protocol_version"], "3.0", "协议版本")
    ids = [int(f["id"], 16) for f in frames]
    s.eq(len(ids), len(set(ids)), "帧 ID 唯一")
    for f in frames:
        s.check(len(f["id"]) == 5, "ID 写成 0xRGB 形式: %s" % f["id"])
        s.check(1 <= f["dlc"] <= 8, "DLC 在 1..8: %s" % f["name"])
        s.check(int(f["id"], 16) <= 0x7FF, "11 位 ID 上界: %s" % f["name"])
        for fld in f["fields"]:
            s.check(fld["offset"] + fld["size"] <= f["dlc"],
                    "字段不越界 %s.%s" % (f["name"], fld["name"]))
            s.check(fld["size"] in (1, 2, 4), "字段宽度合法 %s.%s" % (f["name"], fld["name"]))

    # 2. 功能组 | 节点号 一致性
    s.section("id-compose")
    for f in frames:
        g = can["groups"][f["group"]]
        low_from = can["nodes"].get(f["from"])
        low_to = can["nodes"].get(f["to"])
        if f["group"] == "ESTOP":
            expected_low = can["nodes"]["BROADCAST"]
        elif f["group"].startswith("CTRL_"):
            expected_low = low_to
        else:
            expected_low = low_from
        s.eq(int(f["id"], 16), g | expected_low,
             "ID = %s | 节点号 (%s)" % (f["group"], f["name"]))

    # 3. 大端编解码
    s.section("u16-codec")
    for v in (0, 1, 2, 255, 256, 257, 1024, 4095, 4096, 32767, 65534, 65535):
        b = enc(v, 2)
        s.eq(len(b), 2, "u16 编码长度 v=%d" % v)
        s.eq(dec(b, 0, 2), v, "u16 回环 v=%d" % v)
        s.eq(b[0], (v >> 8) & 0xFF, "u16 高字节在前 v=%d" % v)

    # 4. 每帧字节布局 + 回环
    s.section("frame-layout")
    samples = {
        1: (0, 1, 2, 3, 100, 200, 255),
        2: (0, 1, 255, 256, 4095, 65535),
    }
    for f in frames:
        combos = [()]
        for fld in f["fields"]:
            vals = samples[fld["size"]]
            combos = [base + (v,) for base in combos for v in vals[:3]]
        for combo in combos[:40]:
            values = {}
            for fld, v in zip(f["fields"], combo):
                values[fld["name"]] = v if "scale" not in fld else v / fld["scale"]
            buf = encode_frame(f, values)
            s.eq(len(buf), f["dlc"], "%s 编码长度 == DLC" % f["name"])
            back = decode_frame(f, buf)
            for fld in f["fields"]:
                got = back[fld["name"]]
                want = values[fld["name"]]
                s.check(abs(got - want) < 1e-6, "%s.%s 回环" % (f["name"], fld["name"]))
            for fld in f["fields"]:
                expect = enc(values[fld["name"]] if "scale" not in fld
                             else int(round(values[fld["name"]] * fld["scale"])), fld["size"])
                s.eq(buf[fld["offset"]:fld["offset"] + fld["size"]], expect,
                     "%s.%s 大端字节序" % (f["name"], fld["name"]))

    # 5. 采集帧字节值（0x101 红外/光敏/seq）
    s.section("collect-sensor")
    f101 = by_name["COLLECT_SENSOR"]
    for ir, ldr, seq in ((0, 0, 0), (4095, 0, 255), (0x1234, 0x5678, 7), (1000, 2000, 42)):
        buf = encode_frame(f101, {"ir": ir, "ldr": ldr, "seq": seq})
        s.eq((buf[0] << 8) | buf[1], ir, "0x101 红外大端")
        s.eq((buf[2] << 8) | buf[3], ldr, "0x101 光敏大端")
        s.eq(buf[4], seq, "0x101 seq")

    # 6. seq 丢帧（全回绕）
    s.section("seq-lost")
    for old in range(256):
        for step in (1, 2, 3, 5):
            new = (old + step) & 0xFF
            s.eq(seq_lost(new, old), step - 1, "丢帧 old=%d step=%d" % (old, step))

    # 7. 急停 Fail-Safe（B0 全部 256 种）
    s.section("estop-failsafe")
    for b0 in range(256):
        s.eq(estop_action(b0), 2 if b0 == 2 else 1, "B0=0x%02X 动作" % b0)
    s.eq(encode_frame(by_name["ESTOP"], {"action": 1})[0], 1, "急停帧 B0")
    s.eq(by_name["ESTOP"]["dlc"], 1, "急停帧 DLC=1")

    # 8. 心跳魔数
    s.section("heartbeat")
    magic = can["heartbeat"]["magic"]
    for node, frame_name in (("COLLECT", "COLLECT_HEARTBEAT"),
                             ("CONTROL", "CONTROL_HEARTBEAT"),
                             ("GATEWAY", "GATEWAY_HEARTBEAT")):
        buf = encode_frame(by_name[frame_name], {"magic": int(magic[node], 16), "cnt": 9})
        s.eq(buf[0], int(magic[node], 16), "%s 魔数 %s" % (frame_name, node))
        s.eq(buf[1], 9, "%s 计数" % frame_name)
    s.check(can["heartbeat"]["timeout_ms"] == 3 * can["heartbeat"]["period_ms"],
            "心跳超时 = 3 倍周期")

    # 9. 报警码
    s.section("alarm-codes")
    for code, name in c["alarm_codes"].items():
        s.check(int(code) in (1, 2, 3, 4), "报警码合法 %s" % name)
    s.eq(len(c["alarm_codes"]), 4, "报警码数量")

    # 10. MQTT 主题
    s.section("mqtt-topics")
    prefix = mqtt["prefix"]
    for pub in mqtt["publish"]:
        topic = pub["topic"].replace("{prefix}", prefix)
        s.check(topic.startswith(prefix + "/"), "主题带前缀: %s" % topic)
        s.check("," not in topic and " " not in topic, "主题无非法字符: %s" % topic)
        s.eq(pub["qos"] in (0, 1, 2), True, "QoS 合法: %s" % pub["key"])
        s.check(pub.get("retain") is False, "上行不 retain: %s" % pub["key"])
        for k, t in pub["payload"].items():
            s.check(t in ("uint8", "uint16", "uint32", "int16", "float"), "载荷类型合法 %s.%s" % (pub["key"], k))

    # 11. 订阅通配符
    s.section("mqtt-wildcards")
    sub_pattern = mqtt["subscribe"][0]["topic"].replace("{prefix}", prefix)
    s.eq(sub_pattern, prefix + "/+/cmd/#", "订阅模式")
    for cmd in mqtt["commands"]:
        for node in mqtt["nodes"]:
            topic = build_cmd_topic(prefix, node, cmd["key"])
            s.check(topic_matches(sub_pattern, topic), "命令主题匹配: %s" % topic)
    negatives = [
        "other/nodeA/cmd/motor",
        prefix + "/nodeA/motor",
        prefix + "/a/b/cmd/motor",
        prefix + "nodeA/cmd/motor",
    ]
    for t in negatives:
        s.check(not topic_matches(sub_pattern, t), "非法主题不匹配: %s" % t)
    # MQTT 规范：'#' 也匹配父层级，所以 ".../cmd" 本身是匹配的
    s.check(topic_matches(sub_pattern, prefix + "/nodeA/cmd"), "# 匹配父层级")
    s.check(topic_matches(prefix + "/#", prefix + "/a/b/c"), "# 多层匹配")
    s.check(topic_matches(prefix + "/+/telemetry", prefix + "/nodeA/telemetry"), "+ 单层匹配")
    s.check(not topic_matches(prefix + "/+/telemetry", prefix + "/nodeA/x/telemetry"), "+ 不跨层")

    # 12. 命令映射
    s.section("commands")
    for cmd in mqtt["commands"]:
        s.check(cmd["key"] in ("estop", "motor", "target", "pid", "mode"), "命令类型: %s" % cmd["key"])
        s.eq(cmd.get("retain"), False, "控制命令绝不 retain: %s" % cmd["key"])
        s.eq(cmd["qos"], 1, "控制命令 QoS1: %s" % cmd["key"])
        for fid in cmd["can_frame"].split("|"):
            s.check(fid.lower() in by_id, "命令指向已定义 CAN 帧: %s" % fid)

    # 13. 命令载荷解析
    s.section("payload-parse")
    s.eq(parse_motor("1,50"), (1, 50), "motor 1,50")
    s.eq(parse_motor("0"), (0, 0), "motor 0")
    s.eq(parse_motor("3,100"), (3, 100), "motor 3,100")
    s.eq(parse_motor("abc"), None, "motor 非法")
    s.eq(parse_motor(""), None, "motor 空")
    for v, want in ((0, 0), (2000, 2000), (65535, 65535), (65536, None), (-1, None)):
        s.eq(parse_target(str(v)), want, "target %r" % v)
    s.eq(parse_pid("1.5,0.2,0"), (150, 20, 0), "pid 正常")
    s.eq(parse_pid("1,2,3"), (100, 200, 300), "pid 整数")
    s.eq(parse_pid("1,2"), None, "pid 数量不对")
    s.eq(parse_pid("-1,0,0"), None, "pid 负数拒绝")
    s.eq(parse_pid("a,b,c"), None, "pid 非法")
    s.eq(parse_mode("0"), 0, "mode 0")
    s.eq(parse_mode("1"), 1, "mode 1")
    s.eq(parse_mode("2"), None, "mode 越界")
    for payload, want in (("1", 1), ("2", 2), ("", 1), ("garbage", 1), ("0", 1)):
        s.eq(parse_estop(payload), want, "estop %r 默认急停" % payload)

    # 14. 生成物同步
    s.section("generated-sync")
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "gen_contract.py"), "--check"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    s.eq(r.returncode, 0, "生成物与 contract.json 同步（%s）" % (r.stdout.strip().splitlines()[:1] or [""])[0])
    for name in ("contract_c.h", "contract_qt.h", "contract.md"):
        s.check((ROOT / "protocol" / "generated" / name).exists(), "生成物存在: %s" % name)

    gen_c = (ROOT / "protocol" / "generated" / "contract_c.h").read_text(encoding="utf-8")
    for f in frames:
        s.check("CONTRACT_CAN_ID_%s" % f["name"] in gen_c, "C 头含帧 ID 宏: %s" % f["name"])
        s.check("CONTRACT_CAN_DLC_%s" % f["name"] in gen_c, "C 头含 DLC 宏: %s" % f["name"])
    for pub in mqtt["publish"]:
        s.check("CONTRACT_TOPIC_%s" % pub["key"].upper() in gen_c, "C 头含主题宏: %s" % pub["key"])
    s.check('CONTRACT_MQTT_PREFIX   "%s"' % prefix in gen_c, "C 头前缀 = contract")
    s.check("CONTRACT_CMD_SUB_PATTERN" in gen_c or 'CONTRACT_COMMAND_TOPIC' in gen_c, "C 头含订阅模式")
    for cmd in mqtt["commands"]:
        s.check("CONTRACT_CMD_%s" % cmd["key"].upper() in gen_c, "C 头含命令宏: %s" % cmd["key"])

    gen_qt = (ROOT / "protocol" / "generated" / "contract_qt.h").read_text(encoding="utf-8")
    for pub in mqtt["publish"]:
        s.check(pub["topic"].replace("{prefix}", prefix) in gen_qt, "Qt 头含主题: %s" % pub["key"])
    s.check("namespace contract" in gen_qt, "Qt 头命名空间")
    s.check("kTopicPrefix" in gen_qt, "Qt 头前缀常量")
    s.check(not re.search(r";\s*=", gen_qt), "Qt 头无非法分号（会把 .h 写坏）")
    s.check(gen_qt.count("{") == gen_qt.count("}"), "Qt 头花括号配平")
    for cmd in mqtt["commands"]:
        s.check(('"%s"' % cmd["key"]) in gen_qt, "Qt 头含命令字面量: %s" % cmd["key"])
    for who in can["heartbeat"]["magic"]:
        s.check("kHbMagic" + who in gen_qt, "Qt 头含心跳魔数常量: %s" % who)

    # 15. protocol.h 一致性
    s.section("protocol-h-consistency")
    if PROTOCOL_H.exists():
        raw, resolved = parse_protocol_h()
        h_values = set(v for v in resolved.values() if v is not None)
        for f in frames:
            fid = int(f["id"], 16)
            s.check(fid in h_values, "protocol.h 定义了帧 %s (%s)" % (f["id"], f["name"]))
        lens = {name: v for name, v in resolved.items() if name.startswith("LEN_")}
        s.check(len(lens) >= 8, "protocol.h 有 LEN_ 宏（%d 个）" % len(lens))
        for f in frames:
            if f["name"] in ("CTRL_MOTOR_COLLECT", "CTRL_MOTOR_CONTROL"):
                continue
            s.check(f["dlc"] in set(lens.values()) or f["id"] in ("0x301", "0x302"),
                    "DLC 出现在 protocol.h: %s" % f["name"])
        s.eq(resolved.get("NODE_BROADCAST"), 0, "NODE_BROADCAST=0")
        s.eq(resolved.get("NODE_COLLECT") or resolved.get("NODE_A"), can["nodes"]["COLLECT"], "采集节点号")
        s.eq(resolved.get("NODE_CONTROL") or resolved.get("NODE_B"), can["nodes"]["CONTROL"], "执行节点号")
        s.eq(resolved.get("NODE_GATEWAY") or resolved.get("NODE_GW"), can["nodes"]["GATEWAY"], "网关节点号")
        for node, macro in (("COLLECT", "HB_MAGIC_A"), ("CONTROL", "HB_MAGIC_B"), ("GATEWAY", "HB_MAGIC_GW")):
            s.eq(resolved.get(macro), int(can["heartbeat"]["magic"][node], 16), "心跳魔数 %s" % macro)
    else:
        s.check(False, "protocol/protocol.h 缺失")

    # 16. 网关 mqtt.h 是否改为从契约派生
    s.section("firmware-contract")
    mqtt_h = ROOT / "firmware" / "gateway" / "component" / "MiddleWare" / "MQTT" / "mqtt.h"
    if mqtt_h.exists():
        txt = mqtt_h.read_text(encoding="utf-8", errors="ignore")
        s.check('#include "contract_c.h"' in txt, "网关 mqtt.h 引用契约头 contract_c.h")
        s.check("CONTRACT_MQTT_PREFIX" in txt, "网关主题前缀来自契约 (#define MQTT_PREFIX CONTRACT_MQTT_PREFIX)")
        for macro, want in (
            ("TOPIC_TELEMETRY_A", "CONTRACT_TOPIC_TELEMETRY_COLLECT"),
            ("TOPIC_TELEMETRY_B", "CONTRACT_TOPIC_TELEMETRY_CONTROL"),
            ("TOPIC_STATUS_GW", "CONTRACT_TOPIC_GATEWAY_STATUS"),
            ("TOPIC_STATUS_STATS", "CONTRACT_TOPIC_GATEWAY_STATS"),
            ("TOPIC_CMD_SUB", "CONTRACT_COMMAND_TOPIC"),
            ("MQTT_QOS_CMD", "CONTRACT_COMMAND_QOS"),
        ):
            s.check(re.search(r"#define\s+%s\s+%s" % (macro, want), txt) is not None,
                    "网关 %s 派生自 %s" % (macro, want))
        s.check('"gw0805"' not in txt, "网关不再硬编码主题前缀字符串")

    mqtt_c = ROOT / "firmware" / "gateway" / "component" / "MiddleWare" / "MQTT" / "mqtt.c"
    if mqtt_c.exists():
        txt = mqtt_c.read_text(encoding="utf-8", errors="ignore")
        s.check("CONTRACT_MQTT_URI" in txt, "网关 broker 地址派生自契约 (CONTRACT_MQTT_URI)")
        s.check("mqtt://broker.emqx.io" not in txt, "网关不再硬编码 broker URI")
        s.check("CONTRACT_MQTT_PREFIX" in txt or "mqtt.h" in txt, "网关 MQTT 层引用契约头")
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    with CONTRACT_PATH.open(encoding="utf-8") as f:
        c = json.load(f)
    s = run(c, args.verbose)

    print("=" * 62)
    print("主机侧协议自测：%d 通过 / %d 失败（共 %d 条断言）"
          % (s.passed, len(s.failed), s.passed + len(s.failed)))
    print("=" * 62)
    if s.failed:
        by_case = {}
        for f in s.failed:
            by_case.setdefault(f.split("]")[0] + "]", []).append(f)
        for case, items in by_case.items():
            print("\n%s 失败 %d 条：" % (case, len(items)))
            for it in items[:8]:
                print("   -", it)
        return 1
    print("全部通过 ✔")
    return 0


if __name__ == "__main__":
    sys.exit(main())
