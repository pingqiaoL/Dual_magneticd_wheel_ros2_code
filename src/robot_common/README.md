# robot_common

`robot_common` 是上位机工作区的通用 C++ 基础库。该包不创建 ROS 节点、不保存业务状态，也不知道具体设备协议。

## 公共 API

| 头文件 | 主要接口 | 作用 |
|---|---|---|
| `robot_common/clock/SteadyClock.hpp` | `monotonic_time_now()`、`elapsed_time_ms()`、`remaining_time_ms()` | 进程内周期、年龄和截止时间 |
| `robot_common/math/Saturation.hpp` | `clamp_to_uint32()`、`clamp_to_nonnegative_int()` | 整数范围限制 |
| `robot_common/buffer/ByteRingBuffer.hpp` | 写入、查看、丢弃、清空 | 单线程有界字节缓存 |
| `robot_common/io/SerialPort.hpp` | `open_port()`、`read_bytes()`、`write_bytes()`、`native_handle()`、`close_port()` | Linux TTY raw 8N1 有界 I/O |

`SerialPort` 构造函数只保存路径和波特率，`open_port()` 才访问设备。协议构帧、心跳、设备发现和自动重连必须由具体设备模块完成。

`native_handle()` 只借出当前描述符，供拥有该 `SerialPort` 的同一线程纳入自己的
`poll()` 事件循环；调用者不得关闭描述符、修改配置或在另一个线程并发打开/关闭。
外部唤醒、业务队列和线程调度仍由设备模块负责。

## 目录

```text
robot_common/
├── include/robot_common/
│   ├── buffer/ByteRingBuffer.hpp
│   ├── clock/SteadyClock.hpp
│   ├── io/SerialPort.hpp
│   └── math/Saturation.hpp
├── module/
│   ├── SerialPort.cpp
│   └── SteadyClock.cpp
└── test/
    ├── test_robot_common.cpp
    └── test_serial_port.cpp
```

## 使用边界

- ROS 消息时间戳使用节点时钟；超时、重连和耗时使用单调时钟。
- `ByteRingBuffer` 只存字节，不解释任何帧格式。
- `SerialPort` 可用于 USB CDC、USB 转串口和板载 UART，但不适用于 V4L2 摄像头。
- 仅被一个设备使用的协议和状态机保留在设备包中。

## 构建与测试

```bash
cd /workspace
source /opt/ros/jazzy/setup.bash
colcon build --packages-select robot_common
source /workspace/install/setup.bash
colcon test --packages-select robot_common --event-handlers console_direct+
colcon test-result --verbose
```

新增公共 API 时，必须补充契约说明和边界测试。
