# sys-netsec-lab

高性能网络安全研发工程师学习实验室 — 面向 AI / 云基础设施

**目标**：一年内完成 4 个核心项目 + 1 个旗舰项目，成为可独立负责模块的高性能网络/安全工程师。

**执行周期**：2026-07-27 → 2027-07-25 | 52 周 | 468 条任务

---

## 项目总览

| # | 项目 | 阶段 | 定位 |
|---|------|------|------|
| 1 | Mini TCP Gateway | Phase 1 (W1-W8) | Linux C/C++ / Socket / epoll / 背压 / 测试 |
| 2 | TLS/TLCP 安全接入网关 | Phase 3 (W15-W22) | PKI / TLS 1.3 / mTLS / TLCP / 可观测性 |
| 3 | IPsec 实验与智能诊断平台 | Phase 4 (W23-W30) | IKEv2 / strongSwan / XFRM / AI 诊断 |
| 4 | AI 基础设施高性能安全网关 | Phase 5-7 (W31-W52) | DPDK / 云网络 / Go 控制面 / 遥测 / AI |

## 七阶段路线图

### Phase 1 — 系统基础与 TCP 网关（W1-W8）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W1 | `phase1-tcp-gateway/week1` | 开发环境与编译链 | system-lab：构建脚本、环境自检、静态/动态库 |
| W2 | `phase1-tcp-gateway/week2` | C 内存与调试 | memory-lab：5 类内存缺陷、ASan/UBSan/Valgrind |
| W3 | `phase1-tcp-gateway/week3` | 现代 C++ 与 RAII | raii-lab：FD/内存/线程资源封装与单元测试 |
| W4 | `phase1-tcp-gateway/week4` | 进程线程与同步 | concurrency-lab：线程池、队列、TSan 无竞态 |
| W5 | `phase1-tcp-gateway/week5` | TCP Socket 基础 | Mini TCP Gateway v0：客户端-代理-后端 |
| W6 | `phase1-tcp-gateway/week6` | 非阻塞 IO 与 epoll | Mini TCP Gateway v1：epoll 事件循环 |
| W7 | `phase1-tcp-gateway/week7` | 缓冲区、定时器与背压 | Mini TCP Gateway v2：限流、Metrics |
| W8 | `phase1-tcp-gateway/week8` | TCP 网关项目验收 | 正式版：CI、架构图、性能/故障报告、演示 |

### Phase 2 — 网络原理与协议实现（W9-W14）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W9 | `phase2-network-stack/week9` | 分层封装与抓包 | pcap-lab：抓包模板、协议头解析器 |
| W10 | `phase2-network-stack/week10` | TCP 可靠传输与拥塞 | 可靠 UDP 实验与 TCP 性能分析 |
| W11 | `phase2-network-stack/week11` | 路由/ARP/NAT/MTU | Linux namespace 三节点路由实验 |
| W12 | `phase2-network-stack/week12` | ByteStream 与重组 | Mini Internet Stack v0：ByteStream + Reassembler |
| W13 | `phase2-network-stack/week13` | TCP Sender/Receiver | Mini Internet Stack v1：状态机 |
| W14 | `phase2-network-stack/week14` | 网络接口、ARP 与路由 | Mini Internet Stack 正式版 |

### Phase 3 — TLS/TLCP 安全网关与可观测性（W15-W22）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W15 | `phase3-tls-gateway/week15` | 密码工程与 PKI 基础 | crypto-pki-lab：算法实验、PKI 知识图 |
| W16 | `phase3-tls-gateway/week16` | CA 与证书链 | 可复用测试 PKI 工具包与证书错误矩阵 |
| W17 | `phase3-tls-gateway/week17` | TLS 1.3 状态机 | 协议地图、字段速查表、抓包注释 |
| W18 | `phase3-tls-gateway/week18` | OpenSSL 阻塞式 Client/Server | TLS Client/Server 基础库与测试 |
| W19 | `phase3-tls-gateway/week19` | 非阻塞 TLS 与 epoll | TLS 接入网关 v1：非阻塞握手与转发 |
| W20 | `phase3-tls-gateway/week20` | mTLS、会话与可观测性 | TLS 接入网关 v2：10 类握手失败可分类 |
| W21 | `phase3-tls-gateway/week21` | TLCP 与 Tongsuo 双证书 | TLCP 实验仓库与对照报告 |
| W22 | `phase3-tls-gateway/week22` | TLS/TLCP 网关验收 | 项目二正式版 + 故障数据集 v1 |

### Phase 4 — IKEv2/IPsec 与智能诊断（W23-W30）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W23 | `phase4-ipsec-diagnostics/week23` | IPsec 架构与 IKEv2 状态机 | 架构图、状态机图、术语速查 |
| W24 | `phase4-ipsec-diagnostics/week24` | Namespace 实验底座 | IPsec 实验平台：namespace/veth/route |
| W25 | `phase4-ipsec-diagnostics/week25` | PSK 与 X.509 Site-to-Site | 隧道自动化脚本、抓包和故障矩阵 |
| W26 | `phase4-ipsec-diagnostics/week26` | NAT-T、Rekey、DPD 与 MTU | 隧道稳定性实验报告 |
| W27 | `phase4-ipsec-diagnostics/week27` | XFRM 与系统级排障 | XFRM 采集器与 10 类故障卡 |
| W28 | `phase4-ipsec-diagnostics/week28` | 诊断采集与 DPDK 预热 | diagnostic collector v1 + DPDK 预热 |
| W29 | `phase4-ipsec-diagnostics/week29` | AI 辅助网络诊断 | 诊断助手 v1：检索、Top-3、证据链 |
| W30 | `phase4-ipsec-diagnostics/week30` | IPsec 平台验收 | 项目三正式版 + AI 评测报告 |

### Phase 5 — 云网络与 AI 基础设施网络（W31-W36）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W31 | `phase5-cloud-ai-network/week31` | Linux 收发包路径与 Netfilter | 报文路径图与 nftables 实验 |
| W32 | `phase5-cloud-ai-network/week32` | Conntrack、NAT 容量与试投 | Conntrack/NAT 报告 + 首轮试投 |
| W33 | `phase5-cloud-ai-network/week33` | VXLAN、OVS 与迷你 VPC | Bridge/VLAN/VXLAN/OVS 拓扑 |
| W34 | `phase5-cloud-ai-network/week34` | Go 控制面与 DPDK 预热 | Go 控制面 v1 + OpenAPI + l2fwd |
| W35 | `phase5-cloud-ai-network/week35` | RDMA、RoCE 与拥塞 | AI 网络知识图 + PFC/ECN 实验 |
| W36 | `phase5-cloud-ai-network/week36` | NCCL、AllReduce 与流量模型 | AllReduce 流量模型 + incast 实验 |

### Phase 6 — DPDK 数据面与 AI 遥测（W37-W46）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W37 | `phase6-dpdk-dataplane/week37` | DPDK 环境与 EAL | 一键检查/启动脚本 |
| W38 | `phase6-dpdk-dataplane/week38` | mbuf/mempool/ring/lcore | 核心组件实验仓库 |
| W39 | `phase6-dpdk-dataplane/week39` | testpmd 与 L2 转发 | L2 Forwarder v1 + 基准记录 |
| W40 | `phase6-dpdk-dataplane/week40` | L3 路由、LPM 与 ACL | Mini Data Plane v1：五元组 ACL |
| W41 | `phase6-dpdk-dataplane/week41` | RSS、多队列与 NUMA | 多核扩展报告 |
| W42 | `phase6-dpdk-dataplane/week42` | 性能基准与瓶颈定位 | 假设-测量-改动-验证 报告 |
| W43 | `phase6-dpdk-dataplane/week43` | 流表、状态与 Telemetry | Mini Data Plane v2 |
| W44 | `phase6-dpdk-dataplane/week44` | Cryptodev 与 ipsec-secgw | 阅读报告 + Crypto 原型 |
| W45 | `phase6-dpdk-dataplane/week45` | AI 遥测特征与异常规则 | Telemetry feature pipeline + 异常规则 |
| W46 | `phase6-dpdk-dataplane/week46` | DPDK 数据面验收 | Mini Data Plane 正式版 + 性能报告 |

### Phase 7 — AI 基础设施安全网关与跳槽（W47-W52）

| 周 | 目录 | 主题 | 核心交付 |
|----|------|------|----------|
| W47 | `phase7-gateway-career/week47` | 旗舰项目集成 | 项目四：端到端安全网关原型 |
| W48 | `phase7-gateway-career/week48` | 鲁棒性、Fuzz 与故障数据集 | 30 类故障案例 + 发现-修复-回归 |
| W49 | `phase7-gateway-career/week49` | AI 诊断正式评测 | 评测报告 v2：完整指标 |
| W50 | `phase7-gateway-career/week50` | C++/Linux/网络面试冲刺 | 错题本、回答卡、模拟面试 |
| W51 | `phase7-gateway-career/week51` | TLS/IPsec/DPDK/RDMA 冲刺 | 专项面试手册 + 10 个 STAR 故事 |
| W52 | `phase7-gateway-career/week52` | 集中投递与决策 | 投递漏斗、Offer 比较表 |

---

## 每周执行系统

| 任务类型 | 计划工时 | 说明 |
|----------|----------|------|
| 理论主线 | 2.0h | 知识图/状态机/接口表 |
| 编码 A | 3.0h | 最小可运行增量 + 自动化测试 |
| 编码 B | 3.0h | 集成/可靠性/性能增量 + 设计说明 |
| 实验排障 | 2.5h | 故障注入 → 假设 → 证据 → 修复 → 回归 |
| 项目里程碑 | 3.0h | 代码/测试/README/图/性能/演示 |
| 工作结合 | 1.5h | 脱敏真实模块或 STAR 素材 |
| 算法/编码 | 1.5h | 每周 2-3 道 |
| 英语/标准 | 1.0h | 官方文档 200-300 字摘要 |
| 技术输出与复盘 | 1.5h | 实际工时/AI 使用/阻塞/下周调整 |

---

## 止损规则

| 触发条件 | 动作 |
|----------|------|
| 连续 2 周 < 12h | 只保留项目主线 8h + 基础/复盘 4h |
| 连续 4 周无可运行代码 | 停止新课，清理半成品并交付一个里程碑 |
| W8 不能独立完成 epoll 网关 | 延后阶段2，补 C++/Socket/调试 |
| 试投无面试 | 检查关键词、城市/经验门槛；扩大投递范围 |
| 有面试但项目被追问击穿 | 暂停投递 1 周，补代码细节、故障和性能实验 |

---

## 最终验收清单

- [ ] 四个项目中至少三个可一键运行、可测试、可演示、可讲解
- [ ] 真实工作中至少独立承担一个明确模块
- [ ] 故障案例库 ≥ 30 条，至少 5 条可用于面试深挖
- [ ] 算法题 120-150 道，高频题二刷通过率 ≥ 70%
- [ ] 专项面试题 ≥ 100 道"已掌握"，完成 ≥ 5 次模拟面试
- [ ] DPDK 报告包含完整环境描述和可信性能数据
- [ ] AI 诊断评测包含 Top-3 命中率、引用正确率、拒答率等
- [ ] 安全网关版 + AI 基础设施网络版两套简历
- [ ] 不裸辞；目标总包提升 25%-40%

---

## 环境要求

- **语言**：C/C++17+, Python 3, Go 1.21+, Bash
- **工具链**：GCC/Clang, CMake, Make, GDB, perf
- **分析**：Valgrind, ASan, UBSan, TSan, tcpdump/Wireshark
- **库**：OpenSSL/Tongsuo, strongSwan, DPDK, libpcap
- **OS**：Linux (当前: `7.0.0-27-generic`)

---

> 🤖 本项目由 Claude Code 作为技术导师全程陪跑
