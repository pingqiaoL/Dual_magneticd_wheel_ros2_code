# ROS 接口与参数参考

文档状态：现行  
实现状态：已实现并完成软件验证  
适用版本：2026-10-16 工作区未提交  
更新日期：2026-10-16

## 启动参数

| 参数 | 类型 | 默认值 | 生效方式 | 约束与错误行为 |
|---|---|---:|---|---|
| `device_path` | string | `/dev/ttyACM0` | 节点启动时读取，只读 | 必须是以 `/` 开头且不含 NUL 的设备路径；非法时启动失败 |
| `baud_rate` | int64 | `115200` | 节点启动时读取，只读 | 支持 9600、19200、38400、57600、115200、230400、460800、921600；非法时启动失败 |

参数通过 `measurement_manager/launch/thickness.launch.xml` 传递给 `thickness_node`。波特率是 CDC/TTY 线路配置，不是 USB 总线速率。

## `/thickness/reading`

发布者：`thickness_node`  
订阅者：`measurement_manager_node`  
类型：`robot_interfaces/msg/ThicknessReading`  
QoS：Reliable、Volatile、队列深度 64  
频率：由仪器测量上报决定

| 字段 | 语义 |
|---|---|
| `received_at` | 节点收到并发布测量时的 ROS 时间 |
| `sequence` | 节点进程内的测量序号，重启后清零 |
| `device_sn` | 协议载荷中的 12 字节设备序列号 |
| `value` | 未滤波的协议浮点测量值 |
| `unit` | 当前固定为 `UNIT_UNKNOWN`，不能假定为 µm |
| `material_raw` | 未解释的材料原码 |
| `finite` | 测量浮点值是否为有限数 |

帧长度、帧尾和 CRC 通过只代表报文格式有效，不代表测量精度已经验收。

## `/thickness/status`

发布者：`thickness_node`  
订阅者：`measurement_manager_node`  
类型：`robot_interfaces/msg/ThicknessStatus`  
QoS：Reliable、Volatile、队列深度 1  
频率：2 Hz

| 字段 | 语义 |
|---|---|
| `connection_state` | `DISCONNECTED`、`CONNECTING`、`ONLINE` 或 `STALE` |
| `port_open` | 当前是否持有有效 TTY 文件描述符 |
| `communication_ready` | 端口打开、设备已识别且最近响应未超时 |
| `device_sn` | 已识别设备序列号；未知时全零 |
| `last_response_age_ms` | 最近有效设备应答年龄；从未收到时为 `UINT32_MAX` |
| `last_measurement_age_ms` | 最近测量年龄；从未收到时为 `UINT32_MAX` |
| `valid_frames` | 解析器累计接受的有效帧数 |
| `crc_errors` | 解析器累计 CRC 错误数 |
| `io_errors` | 打开、读取或写入失败导致的设备会话错误数 |
| `last_error` | 最近错误文本，最长 160 字符 |

## 启动命令

Orin 宿主机：

```bash
bash /home/nvidia/robot_ros2/tools/read_thickness.sh \
  device_path:=/dev/ttyACM0 baud_rate:=115200
```

ROS 容器：

```bash
bash /workspace/tools/read_thickness.sh \
  device_path:=/dev/ttyACM0 baud_rate:=115200
```
