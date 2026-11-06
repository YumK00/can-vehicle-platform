# protocol —— 通信契约的唯一真源

这个目录解决一个具体问题：**同一条通信协议被四五个工程各抄一份，改一处忘三处**。
做法是把协议与 MQTT 主题写成机器可读的单一真源，再由工具生成各端头文件，
最后由校验脚本拦住"只改了自己那一份"的情况。

## 文件

| 文件 | 说明 |
|---|---|
| `contract.json` | **唯一真源**。CAN 帧表（ID/DLC/字段/方向）、功能组、节点号、心跳参数、MQTT 主题、命令映射、报警码，全部写在这里 |
| `protocol.h` | CAN 应用层协议 v3.0 的 C 头（纯头文件，无 .c、无全局变量），三端共用；手工维护的字节级编解码实现 |
| `generated/contract_c.h` | 由 `tools/gen_contract.py` 生成，给 C 端（ESP32 网关） |
| `generated/contract_qt.h` | 同上，给 Qt 端（桌面监控台 / 手机 App） |
| `generated/contract.md` | 同上，生成人能读的协议与主题表 |

> `contract.json` 是"谁是权威"；`protocol.h` 是"具体怎么编解码"。
> 两者由 `tools/test_contract.py` 交叉校验（帧 ID 与 DLC 必须一致）。

## 改协议的流程

```bash
# 1. 改唯一真源
vim protocol/contract.json

# 2. 重新生成 C / Qt 头文件与文档
python tools/gen_contract.py

# 3. 同步到各工程副本
python tools/sync_contract_copies.py

# 4. 跑主机侧协议自测（3500+ 条断言，无需硬件）
python tools/test_contract.py

# 5. 校验所有副本没有被单独篡改
python tools/validate_copies.py
```

第 3 步同步的副本：

| 真源 | 副本 |
|---|---|
| `protocol/protocol.h` | `firmware/gateway/component/MiddleWare/PROTOCOL/protocol.h` |
| `protocol/protocol.h` | `firmware/collect-node/Core/Inc/protocol.h` |
| `protocol/protocol.h` | `firmware/control-node/Core/Inc/protocol.h` |
| `generated/contract_c.h` | `firmware/gateway/component/MiddleWare/PROTOCOL/contract_c.h` |
| `generated/contract_qt.h` | `host/desktop-console/contract_qt.h` |
| `generated/contract_qt.h` | `host/mobile-app/contract_qt.h` |

## 协议要点（详见 `generated/contract.md`）

- **ID 结构**：11 位标准帧，`bit10..4` 功能组（16 的倍数）+ `bit3..0` 节点号。
  功能组分 `0x0xx` 紧急/报警、`0x1xx` 周期上报、`0x3xx` 控制下发、`0x7xx` 心跳。
- **字节序**：多字节一律**大端**，收发都走 `PROTO_PutU16/GetU16` 手工移位，
  不做指针强转（小端 CPU + 非对齐访问双重风险）。
- **DLC 严格**：接收侧先比对 `data_length_code == LEN_xxx`，不等即丢。
- **可靠性三件套**：心跳魔数判活（超时 3 s）、`seq` 序列号算丢帧、`cid` 命令序号做幂等。
- **Fail-Safe**：`0x000` 急停帧 B0 只要不是 `2`（解除）就一律按急停执行，
  解析失败、负载为空、收到脏数据都停机 —— 安全帧不允许"看不懂就放行"。

## 为什么 MQTT 主题也要进契约

主题串一旦分散在各端源码里，最容易出现"网关发 `gw0805/nodeA/telemetry`、
上位机收 `gw0805/nodeA/telemetry/`"这种一个字符的错。
把主题、QoS、载荷字段名一起写进 `contract.json` 并生成常量后，
三端只可能引用同一个字符串，从源头消除这类联调事故。
