# 上下位机 XRCE DDS 通信可行性

日期：2026-10-14。本文保留技术评估结论；用户随后明确要求将目标软件框架调整为 XRCE-DDS，README 和节点图已同步。此变更仅为文档架构，尚未安装 Agent、移植 Client 或完成实机验证。

## 结论与架构

可以采用 PX4 的 Client/Agent 思路：STM32/NuttX 上的通信模块订阅现有本地总线，使用 Micro XRCE-DDS Client 与 Orin 上的 Agent 经专用 UART 通信；Agent 在 DDS 网络中代理发布和订阅，ROS 2 业务节点使用对应类型的话题。MCU 不需要安装完整 ROS 2 或完整桌面 DDS 实现。

```text
STM32 / NuttX                                 Orin / ROS 2
本地模块 ↔ 本地发布订阅总线 ↔ XRCE Client ←UART→ Agent ↔ DDS ↔ 业务节点
```

PX4 使用 uORB 到 XRCE 的桥接与代码生成。当前项目是自定义 NuttX 固件、轻量 Topic 总线，并非完整 PX4，不能直接复制 PX4 模块就视为完成移植。可借鉴它的职责分层，按本项目接口适配。

## 已核对的项目条件

- 下位机 canonical defconfig 指定 STM32F407IG；CONFIG_RAM_SIZE=114688，即当前配置的 RAM 区域是 112 KiB。这不代表芯片所有 RAM，也不能据此计算当前剩余内存。需要结合链接脚本、map/size 报告、运行时空闲堆和任务栈余量验证。
- robot/orb 是项目自有的轻量消息总线，适合保留；只将需要交换的状态和命令接入通信层。
- 本次搜索项目 robot 与 protocol 源文件，未发现已实现的 XRCE-DDS 通信模块。此前的 MAVLink 上下位机通信尚未实现；当前目标架构已经改为 XRCE-DDS，历史工具保留用于环境验证。
- 上位机使用 ROS 2 Jazzy、Cyclone DDS、ROS_DOMAIN_ID=42。Agent 内部使用 Fast DDS；跨实现互通需核对类型、话题名、QoS、域和发现配置，再实测确认，不能只看进程启动。

## 建议的移植路线

1. 在 PC/Orin 上先验证 Client 与 Agent 的最小示例，固定消息定义与依赖版本。
2. 下位机加入裁剪后的 Micro XRCE-DDS Client 与 Micro CDR，适配 NuttX 串口、时间和构建系统。只启用使用的传输与功能，不引入完整 PX4。
3. 在独立通信任务中维护会话、实体、收发、断开与重连，不让阻塞收发占用控制或安全工作队列。
4. 第一次只发送小尺寸状态，再增加一个不驱动执行器的命令及应用层应答。测试中断、Agent 重启、消息版本不匹配等行为。
5. 测量 Flash、静态 RAM、堆、通信任务栈和 CPU 开销；按实际消息大小、频率、MTU 和可靠流历史调整。
6. 双方共享接口定义和序列化规则。不能将本地 C++ struct 原样 memcpy 当作兼容 ROS 2 的 CDR；话题与类型命名也要遵循 ROS 2 DDS 映射。
7. 目标文档已按用户要求改为此方案；最小验证通过后再推进真实实现，按验证结果更新状态。若需改变框架树，先取得用户审批后再同步文档。

PX4 风格的轻量 XRCE Client 桥接适合保留现有下位机模块；micro-ROS 则在下位机增加 ROS 风格的 C API、执行器和相关依赖，是另一条路线，两者不等同。选型需按维护需求与资源实测决定。

## 版本和语义注意事项

- 本次查阅的 PX4 官方版本表列出 ROS 2 Jazzy 对应 Micro-XRCE-DDS-Agent 2.4.3 与 v2.x 客户端路线。正式实施时固定相互兼容的 Client、Agent、Fast DDS 与消息定义，不能随意混用不同大版本的最新标签。
- Agent 的 DDS participant 需要加入与 ROS 节点一致的域。本项目为 42，不能假定给普通 ROS 节点设置环境变量就自动配置了独立 Agent。
- 使用 XRCE Client 不会自动获得全部 ROS service/action 接口；可先以命令话题和应答话题实现应用协议，确需 ROS 服务时再适配请求/响应映射。
- DDS 可靠传输不等于动作执行成功。命令仍需序号、业务应答、超时和重复处理规则。
- DDS 通信状态不直接作为解锁许可。电机、推杆、安全互锁和断链后的控制策略仍由下位机已有的 Command/Failsafe 等模块处理。
- 原有天空端到地面端的数传协议、视频网口传输及 USB 测厚仪读取不必随 UART 上的上下位机协议一起改动。

## 参考资料

- [PX4 uXRCE-DDS 架构与版本表](https://docs.px4.io/main/en/middleware/uxrce_dds)
- [Micro XRCE-DDS Client 支持平台与裁剪配置](https://micro-xrce-dds.docs.eprosima.com/en/stable/client.html)
- [Micro XRCE-DDS Agent 与 DDS、序列化类型关系](https://micro-xrce-dds.docs.eprosima.com/en/stable/introduction.html)
