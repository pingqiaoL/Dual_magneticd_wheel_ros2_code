# 测厚仪 USB 协议实机测试

日期：2026-10-15（Asia/Shanghai）。本目录包含独立协议诊断与跨节点测试，不是ROS包。早期测试后，现已按用户授权创建正式业务源码。

## 最终验证结果

- 已成功收到厂家广播、心跳、状态应答及测量上报。
- 连续会话 `results/vendor_live_verified`：TX 54条、2168 B，RX 4034 B、95帧。广播1、心跳51、状态2、测量41；95帧全部通过载荷CRC，无坏帧候选。
- 设备业务SN：`CT260918B002`。USB描述符SN `FFFFFFFEFFFF` 与其不同。
- CRC：仅覆盖载荷，CRC-16/MODBUS，结果高字节先传。与标准Modbus RTU的CRC低字节先传不同。
- 测量L=17，整帧45 B；纠正原文的16/44 B。
- 状态请求载荷12 B与SN后补零到20 B均返回相同48 B应答。建议先使用明确的12 B请求。
- 状态原码：当前探头1、校准探头1、菜单0、设备探头0；校准厚度字段大端float为1000.0，不是当前测量值。
- 41帧测量按大端float解码范围177.1884765625至279.36328125，材料原码1。尚未与49 µm标称校准片及手机示值同步对照，不能判定计量准确性通过。
- 独立复测 `vendor_exact_verified`：TX 80 B，RX 183 B，共广播1、心跳1、测量2；含发送请求前已收到的一条测量。

## 执行位置

宿主机 `/home/nvidia/robot_ros2/test`，容器对应 `/workspace/test`。宿主机nvidia没有dialout权限，容器robot有权限。端口 `/dev/ttyACM0`，目标VID/PID `28e9:018a`。

在SSH宿主机终端执行：

```bash
cd /home/nvidia/robot_ros2
docker exec -it robot_ros2_dev bash
```

进入容器执行：

```bash
cd /workspace
source /opt/robot-venv/bin/activate
python3 test/usb_protocol_lab.py describe
python3 test/usb_protocol_lab.py selftest
python3 test/vendor_probe.py --duration 15 --out /workspace/test/results/manual_vendor_01
python3 test/vendor_readonly.py --duration 50 --out /workspace/test/results/manual_live_01
python3 test/verify_received.py /workspace/test/results/manual_live_01/rx.bin
```

采集期间反复贴合和抬离探头。每次更换输出目录，脚本会覆盖同名结果。不要同时运行其他读取该端口的程序。

## 文件作用

- `vendor_probe.py`：原样发送用户提供的厂家广播、心跳。功能码01，不发送配置。可安全导入常量，不会在导入时开启采集。
- `vendor_readonly.py`：广播、心跳、当前设备SN状态查询，以及约1 Hz持续心跳和接收。SN固定用于本台探头，换设备先发现新SN。可导入query函数自检。
- `verify_received.py`：按大端长度L组帧、检查帧尾和载荷CRC；提取状态、材料原码、大端和小端浮点解释，保存 `rx.verified.json`。
- `raw_capture.py`：POSIX open、tcsetattr(TCSANOW)、select、read，被动读取，不主动tcflush，退出恢复termios；不是USB总线抓包。
- `usb_protocol_lab.py`：设备描述、早期被动和候选查询、8种CRC检查向量、粗略离线分析。analyze已补充payload CRC覆盖范围；其中query只覆盖早期未成功候选，当前不优先使用。
- `results/summary.json`：会话汇总；每个会话保存capture.json、rx.bin、tx.bin。JSON标签代表收发等待窗口，不一定代表测量与心跳具有因果关系。

## 历史记录与边界

厂家给出两条正确请求之前，10次完整候选会话TX 168条6720 B、RX=0，其中未覆盖实际的“仅载荷CRC”规则。最初另一次8秒被动读取RX=0但JSON保存失败，未计入完整会话。

中途一次模块导入触发重复采集，已受控中断并加main保护，保留了诊断记录。`vendor_115200`与`vendor_import_interrupted`可能包含该中断记录，不计入成功会话；有效复核目录以两个verified目录为准。

无校准、保存系数、改模式或写地址。完整最小启动流程、心跳是否必须、超时门限、材料码含义、异常值和广播额外字段仍待确认。原文的更新类命令未做实机测试。

详细Word保存在Windows原测厚仪资料目录：“涂层测厚仪USB协议详细说明与实测记录20261015.docx”。后续用户已授权正式节点实现，见下方新增验证。采集数据不纳入Git，源码和文档尚未提交或推送。

## 10.15 简化后的当前验证入口

上文厂商协议和Python实测记录保留。旧双节点业务集成和旧USB smoke脚本已随门控/滤波/CSV撤回删除，原结果保留为历史数据，不能当作当前结构测试。

当前包内测试是thickness_usb_driver的thickness_protocol和usb_port，共2项。宿主机.sh、安装后的XML和实际C++节点已用PTY模拟器验证8条原始值/NaN打印、坏CRC拒绝、重连、重复启动保护和SIGINT/SIGTERM退出。验证辅助程序仅保存在忽略的log目录，没有再加入新的业务测试节点。

真实USB15秒只读检查收到仪器应答并识别CT260918B002，当前测量上报0条，unit=unknown；真实厚度和精度需贴合试片并同步对照手机。使用方式见docs/测厚节点使用说明.md，具体报告路径见当天开发文档。采集数据仍不提交Git。
