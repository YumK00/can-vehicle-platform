# tools —— 契约治理与主机侧自测工具链

这些脚本的作用是：**在没有硬件的情况下，把"多端协议不一致"和"命令解析写错"这两类问题挡在联调之前。**

| 脚本 | 作用 | 典型命令 |
|---|---|---|
| `gen_contract.py` | 从 `protocol/contract.json` 生成 C / Qt 头文件与协议文档；`--check` 只校验是否同步 | `python tools/gen_contract.py` |
| `sync_contract_copies.py` | 把真源同步到各工程副本（网关、两个 STM32 节点、两个 Qt 端） | `python tools/sync_contract_copies.py` |
| `validate_copies.py` | 校验各副本与真源逐字节一致，报出协议分叉 | `python tools/validate_copies.py` |
| `test_contract.py` | 主机侧协议自测：3500+ 条断言，覆盖帧布局/大端/丢帧/急停 Fail-Safe/主题通配/命令解析 | `python tools/test_contract.py` |
| `serial_logger.py` | 串口采集器：把节点串口输出落盘成 CSV，用于离线复现与数据集采集 | `python tools/serial_logger.py -p COM3 -b 115200` |

## 自测套件覆盖什么

`test_contract.py` 不依赖任何硬件、也不依赖第三方库（只用标准库），可以直接放进 CI：

| 用例组 | 覆盖内容 | 断言数 |
|---|---|---|
| `structure` | 帧 ID 唯一、ID ≤ 0x7FF、DLC ∈ 1..8、字段不越界 | 100+ |
| `id-compose` | 每帧 ID 都等于 `功能组 \| 节点号`（控制帧取目标节点，其它取发送节点） | 15 |
| `u16-codec` | 大端编解码回环 + 高字节在前逐字节校验 | 36 |
| `frame-layout` | 15 帧逐字段编码 → 字节校验 → 解码回环 | 1000+ |
| `seq-lost` | `PROTO_SeqLost` 全回绕覆盖（256 个基准 × 4 种步长） | 1024 |
| `estop-failsafe` | 急停 B0 全部 256 种取值都只允许"解除/急停"两种结果 | 258 |
| `mqtt-topics` | 上行主题前缀、QoS、不 retain、载荷字段类型 | 100+ |
| `mqtt-wildcards` | `+` / `#` 通配匹配与非法主题拒绝 | 20+ |
| `commands` | 命令 → CAN 帧映射、控制命令必须 QoS1 且不 retain | 20 |
| `payload-parse` | `motor/target/pid/mode/estop` 载荷解析与越界拒绝 | 25 |
| `generated-sync` | 生成物与 `contract.json` 同步、生成头里宏齐全 | 100+ |
| `protocol-h-consistency` | `protocol.h` 与契约的帧 ID / DLC / 节点号 / 心跳魔数一致 | 100+ |
| `firmware-contract` | 网关 `mqtt.h` 主题宏是否真的从契约派生（没被硬编码回去） | 9 |

合计 **3500+ 条断言**，一次运行 1 秒内跑完。

## 为什么值得做

联调最贵的时间不是写代码，是**定位"到底哪一端写错了"**。
协议分叉的典型症状是"网关日志显示发了，上位机就是收不到"，
然后三端各查一遍。把这些检查前移到主机侧自测后：

- 改协议忘了同步 → `validate_copies.py` 直接报文件路径；
- 改了生成器忘了重新生成 → `test_contract.py` 的 `generated-sync` 失败；
- 命令解析边界写错 → `payload-parse` 用例先红；
- 急停兜底写反 → `estop-failsafe` 的 256 条断言直接拦下。

## 一键跑全套

```bash
python tools/gen_contract.py --check && \
python tools/validate_copies.py && \
python tools/test_contract.py
```
