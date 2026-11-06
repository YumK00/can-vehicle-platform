#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_contract.py —— 从单一真源 protocol/contract.json 生成各端头文件与文档。

生成物：
  protocol/generated/contract_c.h   给 C（ESP32 网关 / 需要时给 STM32）
  protocol/generated/contract_qt.h  给 Qt（桌面监控台 / 手机 App）
  protocol/generated/contract.md    人能读的协议与主题表

用法：
  python tools/gen_contract.py            # 生成
  python tools/gen_contract.py --check    # 只校验生成物是否与真源同步（CI/自测用），不同步返回 1
"""

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTRACT = ROOT / "protocol" / "contract.json"
OUT_DIR = ROOT / "protocol" / "generated"

BANNER = (
    "/*\n"
    " * 本文件由 tools/gen_contract.py 从 protocol/contract.json 自动生成，请勿手改。\n"
    " * 重新生成： python tools/gen_contract.py\n"
    " */\n"
)


def load():
    with CONTRACT.open(encoding="utf-8") as f:
        return json.load(f)


def split_prefix(topic: str, prefix: str):
    """把 '{prefix}/xxx' 拆成 C 里的 'PREFIX "xxx"' 形式。"""
    if topic.startswith("{prefix}"):
        return topic[len("{prefix}"):]
    if topic.startswith(prefix):
        return topic[len(prefix):]
    return topic


def can_id_int(frame):
    return int(frame["id"], 16)


def gen_c_header(c):
    mqtt = c["mqtt"]
    can = c["can"]
    prefix = mqtt["prefix"]
    L = []
    L.append(BANNER)
    L.append("#ifndef CONTRACT_C_H\n#define CONTRACT_C_H\n")
    L.append('#define CONTRACT_VERSION "%s"' % c["contract_version"])
    L.append('#define CONTRACT_PROTOCOL_VERSION "%s"\n' % c["protocol_version"])

    L.append("/* ===== MQTT：Broker 与主题前缀 ===== */")
    b = mqtt["broker"]
    scheme = "mqtts" if b["tls"] else "mqtt"
    L.append('#define CONTRACT_MQTT_HOST     "%s"' % b["host"])
    L.append("#define CONTRACT_MQTT_PORT     %d" % b["port"])
    L.append("#define CONTRACT_MQTT_TLS      %d" % (1 if b["tls"] else 0))
    L.append('#define CONTRACT_MQTT_URI      "%s://%s:%d"' % (scheme, b["host"], b["port"]))
    L.append('#define CONTRACT_MQTT_PREFIX   "%s"\n' % prefix)

    L.append("/* ===== MQTT：发布主题（网关 → 云端/上位机） ===== */")
    for pub in mqtt["publish"]:
        key = pub["key"].upper()
        suffix = split_prefix(pub["topic"].replace("{prefix}", prefix), prefix)
        L.append('#define CONTRACT_TOPIC_%-22s CONTRACT_MQTT_PREFIX "%s"' % (key, suffix))
        L.append("#define CONTRACT_QOS_%-24s %d" % (key, pub["qos"]))
        L.append("#define CONTRACT_RETAIN_%-21s %d" % (key, 1 if pub.get("retain") else 0))
    L.append("")

    L.append("/* ===== MQTT：订阅主题（下发命令） ===== */")
    for sub in mqtt["subscribe"]:
        suffix = split_prefix(sub["topic"].replace("{prefix}", prefix), prefix)
        L.append('#define CONTRACT_%s_TOPIC CONTRACT_MQTT_PREFIX "%s"' % (sub["key"].upper(), suffix))
        L.append("#define CONTRACT_%s_QOS   %d" % (sub["key"].upper(), sub["qos"]))
    L.append("")

    L.append("/* ===== MQTT：命令类型 → CAN 帧 ===== */")
    for cmd in mqtt["commands"]:
        key = cmd["key"].upper()
        L.append('#define CONTRACT_CMD_%-10s "/cmd/%s"' % (key, cmd["key"]))
        first_id = cmd["can_frame"].split("|")[0]
        L.append("#define CONTRACT_CMD_%s_FRAME   %su" % (key, first_id))
    L.append("")

    L.append("/* ===== 节点名 ↔ 节点号 ===== */")
    for name, node in mqtt["nodes"].items():
        L.append('#define CONTRACT_NODE_NAME_%-8s "%s"' % (node.upper(), name))
        L.append("#define CONTRACT_NODE_%s %d" % (node.upper(), can["nodes"][node]))
    L.append("")

    L.append("/* ===== CAN：帧 ID 与 DLC（与 protocol.h 一致） ===== */")
    for f in can["frames"]:
        name = f["name"]
        L.append("#define CONTRACT_CAN_ID_%-22s %su" % (name, f["id"]))
        L.append("#define CONTRACT_CAN_DLC_%-21s %du" % (name, f["dlc"]))
    L.append("")

    L.append("/* ===== CAN：心跳参数 ===== */")
    hb = can["heartbeat"]
    L.append("#define CONTRACT_HB_PERIOD_MS   %du" % hb["period_ms"])
    L.append("#define CONTRACT_HB_TIMEOUT_MS  %du" % hb["timeout_ms"])
    for who, magic in hb["magic"].items():
        L.append("#define CONTRACT_HB_MAGIC_%-7s %su" % (who, magic))
    L.append("")

    L.append("/* ===== CAN：总线参数 ===== */")
    L.append("#define CONTRACT_CAN_BITRATE_KBPS   %d" % can["bitrate_kbps"])
    L.append("#define CONTRACT_CAN_SAMPLE_POINT   %.3ff" % can["sample_point"])
    L.append("")

    L.append("/* ===== JSON 字段名（上行遥测/状态） ===== */")
    keys = []
    for pub in mqtt["publish"]:
        for k in pub["payload"].keys():
            if k not in keys:
                keys.append(k)
    for k in keys:
        L.append('#define CONTRACT_KEY_%-16s "%s"' % (k.upper(), k))
    L.append("")

    L.append("/* ===== 报警码 ===== */")
    for code, name in c["alarm_codes"].items():
        L.append("#define CONTRACT_ALM_%-16s %su" % (name, code))
    L.append("")
    L.append("#endif /* CONTRACT_C_H */\n")
    return "\n".join(L)


def gen_qt_header(c):
    mqtt = c["mqtt"]
    can = c["can"]
    prefix = mqtt["prefix"]
    L = [BANNER]
    L.append("#ifndef CONTRACT_QT_H\n#define CONTRACT_QT_H\n")
    L.append("/* 给 Qt 端用的契约常量：纯 const char*，不依赖任何 Qt 头文件，")
    L.append(" * 需要 QString 时用 QString::fromLatin1(contract::kXxx) 转换。 */\n")
    L.append("namespace contract {\n")
    L.append('inline constexpr const char *kVersion = "%s";' % c["contract_version"])
    L.append('inline constexpr const char *kProtocolVersion = "%s";\n' % c["protocol_version"])
    L.append('inline constexpr const char *kBrokerHost = "%s";' % mqtt["broker"]["host"])
    L.append("inline constexpr int kBrokerPort = %d;" % mqtt["broker"]["port"])
    L.append("inline constexpr bool kBrokerTls = %s;" % ("true" if mqtt["broker"]["tls"] else "false"))
    L.append('inline constexpr const char *kTopicPrefix = "%s";\n' % prefix)

    L.append("// 网关发布、上位机订阅")
    for pub in mqtt["publish"]:
        topic = pub["topic"].replace("{prefix}", prefix)
        L.append('inline constexpr const char *k%-28s = "%s";' % (pascal(pub["key"]), topic))
    L.append("")
    L.append("// 网关订阅、上位机发布")
    for sub in mqtt["subscribe"]:
        topic = sub["topic"].replace("{prefix}", prefix)
        L.append('inline constexpr const char *k%-28s = "%s";' % (pascal(sub["key"]) + "Subscribe", topic))
    L.append("")

    L.append("// 命令类型（拼主题用：kTopicPrefix + \"/\" + node + \"/cmd/\" + kind）")
    for cmd in mqtt["commands"]:
        L.append('inline constexpr const char *kCmd%-10s = "%s";' % (pascal(cmd["key"]), cmd["key"]))
    L.append("")

    L.append("// 节点名")
    L.append('inline constexpr const char *kNodeCollect = "%s";' % [k for k, v in mqtt["nodes"].items() if v == "COLLECT"][0])
    L.append('inline constexpr const char *kNodeControl = "%s";' % [k for k, v in mqtt["nodes"].items() if v == "CONTROL"][0])
    L.append('inline constexpr const char *kNodeAll     = "%s";' % [k for k, v in mqtt["nodes"].items() if v == "BROADCAST"][0])
    L.append("")

    L.append("// CAN 帧 ID / DLC")
    for f in can["frames"]:
        L.append("inline constexpr unsigned kCanId%-22s = %su;" % (pascal(f["name"]), f["id"]))
        L.append("inline constexpr unsigned kCanDlc%-21s = %du;" % (pascal(f["name"]), f["dlc"]))
    L.append("")

    L.append("// 心跳")
    L.append("inline constexpr unsigned kHbPeriodMs  = %du;" % can["heartbeat"]["period_ms"])
    L.append("inline constexpr unsigned kHbTimeoutMs = %du;" % can["heartbeat"]["timeout_ms"])
    for who, magic in can["heartbeat"]["magic"].items():
        L.append("inline constexpr unsigned kHbMagic%-10s = %su;" % (pascal(who), magic))
    L.append("")

    L.append("// 品牌/工位信息")
    L.append('inline constexpr const char *kPlatform = "%s";' % c["platform"])
    L.append("} // namespace contract\n")
    L.append("#endif /* CONTRACT_QT_H */\n")
    return "\n".join(L)


def pascal(s):
    return "".join(p[:1].upper() + p[1:] for p in s.replace("-", "_").split("_"))


def gen_markdown(c):
    can, mqtt = c["can"], c["mqtt"]
    L = ["# 通信契约（自动生成）\n",
         "> 由 `protocol/contract.json` 生成，请勿手改。重新生成：`python tools/gen_contract.py`\n",
         "## 1. 总线参数\n",
         "| 项 | 值 |", "|---|---|",
         "| 标准 | %s |" % can["standard"],
         "| 位速率 | %d kbps |" % can["bitrate_kbps"],
         "| 采样点 | %.1f%% |" % (can["sample_point"] * 100),
         "| 字节序 | %s |" % can["byte_order"],
         "| 心跳超时 | %d ms |" % can["heartbeat"]["timeout_ms"], ""]
    L += ["## 2. 节点与功能组\n", "| 节点 | 编号 |", "|---|---|"]
    for n, v in can["nodes"].items():
        L.append("| %s | %d |" % (n, v))
    L += ["", "| 功能组 | 基址 |", "|---|---|"]
    for g, v in can["groups"].items():
        L.append("| %s | 0x%03X |" % (g, v))
    L += ["", "## 3. CAN 帧表\n",
          "| ID | DLC | 名称 | 方向 | 字段 |", "|---|---|---|---|---|"]
    for f in can["frames"]:
        fields = ", ".join("%s[%d:%d]" % (x["name"], x["offset"], x["offset"] + x["size"]) for x in f["fields"])
        L.append("| `%s` | %d | %s | %s → %s | %s |" % (f["id"], f["dlc"], f["name"], f["from"], f["to"], fields))
    L += ["", "## 4. MQTT 发布主题\n", "| 主题 | QoS | Retain | 载荷 | 来源 |", "|---|---|---|---|---|"]
    for p in mqtt["publish"]:
        payload = ", ".join("%s:%s" % (k, v) for k, v in p["payload"].items())
        L.append("| `%s` | %d | %s | `%s` | %s |" % (p["topic"], p["qos"], "是" if p.get("retain") else "否", payload, p.get("source", "")))
    L += ["", "## 5. MQTT 订阅主题\n", "| 主题 | QoS | 说明 |", "|---|---|---|"]
    for s in mqtt["subscribe"]:
        L.append("| `%s` | %d | %s |" % (s["topic"], s["qos"], s.get("note", "")))
    L += ["", "## 6. 命令映射\n", "| 命令 | 主题 | CAN 帧 | 载荷 |", "|---|---|---|---|"]
    for cmd in mqtt["commands"]:
        L.append("| %s | `%s` | `%s` | %s |" % (cmd["key"], cmd["topic"], cmd["can_frame"], cmd["payload"]["format"]))
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只校验生成物是否同步")
    args = ap.parse_args()

    c = load()
    outputs = {
        OUT_DIR / "contract_c.h": gen_c_header(c),
        OUT_DIR / "contract_qt.h": gen_qt_header(c),
        OUT_DIR / "contract.md": gen_markdown(c),
    }

    if args.check:
        stale = []
        for path, text in outputs.items():
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(str(path.relative_to(ROOT)))
        if stale:
            print("[STALE] 生成物与 contract.json 不同步：")
            for s in stale:
                print("   -", s)
            return 1
        print("[OK] 生成物与 contract.json 同步")
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for path, text in outputs.items():
        path.write_text(text, encoding="utf-8", newline="\n")
        print("generated:", path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
