#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('platform', choices=('nano', 'android'))
args = parser.parse_args()
suite = Path(__file__).resolve().parent
project = Path('/workspace') if args.platform == 'nano' else suite.parents[1]
if args.platform == 'nano':
    updates = {
        'docs/modules/视频模块.md': [('视频最大帧年龄默认 500 ms', '视频最大帧年龄默认 150 ms'), ('appsrc 和编码前原始帧 queue 各最多保留 2 帧', 'appsrc 和编码前原始帧 queue 各最多保留 1 帧')],
        'docs/reference/ROS接口与参数.md': [('max_frame_age_ms=500', 'max_frame_age_ms=150')],
    }
    for relative, replacements in updates.items():
        path = project / relative
        text = path.read_text()
        for old, new in replacements:
            assert old in text, relative
            text = text.replace(old, new)
        path.write_text(text)
    path = project / 'src/video_streamer_transmission/README.md'
    path.write_text(path.read_text() + '\n2026-10-20 延迟优化：appsrc 与编码前原始帧队列各保留 1 帧，过载丢旧；最大帧年龄默认 150 ms，分别检查 ROS 入口消息年龄与工作线程等待。该门限不代表曝光到显示的总延迟上限。实测与回滚位置见 `test/camera_delay/OPTIMIZATION.md`。\n')
else:
    path = project / 'README.md'
    text = path.read_text()
    marker = '\n## 当前界面\n'
    assert marker in text
    text = text.replace(marker, '\n0.8.10 视频优化：追帧目标 100 ms、触发 180 ms、持续 200 ms 后开始追赶；已完成真实无线链路对照和短暂停顿恢复测试。[实测与部署记录](test/camera_delay/OPTIMIZATION.md)。\n' + marker, 1)
    path.write_text(text)
header = '## 视频延迟优化落地（100/180/200 与单帧队列）'
record = project / 'docs/2026-10-20开发文档.md'
text = record.read_text() if record.exists() else '# 2026-10-20 开发文档\n'
if header not in text:
    text += '\n\n' + header + '''

用户批准执行延迟优化。测试副本、脚本、数据与报告集中于 test/camera_delay。安卓比较 100/180、80/150、100/150、120/180 四个目标/触发组合，持续门限均为 200 ms。100/180 方案缓存中位数 152 ms、P95 168 ms、约 29.88 FPS，90 秒内 3 次短缓冲共 53 ms；80/150 和 100/150 虽更低但短缓冲显著增加，未采用。最终安卓版本 0.8.10/versionCode 25，34 项单元测试及构建通过，已安装。

Nano appsrc 与编码前原始帧 queue 各从 2 帧改为 1 帧，默认 max_frame_age_ms 从 500 改为 150，保留 720p30、6000 kbit/s、veryfast、zerolatency、无 B 帧和 GOP30。独立副本通过 RTSP 解码、双客户端、端口冲突恢复、停止/重启、输入超时、旧帧拒绝与退出释放测试。真实链路模拟 250 ms 和 800 ms 发送进程暂停，800 ms 触发预期 STALL_TIMEOUT/自动重连，恢复后缓存回到约 150 ms；不把主动注入产生的卡顿计为自然故障。

正式构建首次因测试副本参与包扫描而被拒绝，源文件恢复、原入口重启；随后添加 test/camera_delay/COLCON_IGNORE，并明确 --base-paths src，正式编译通过。复核当前进程加载正式安装库而非测试副本，正式源码与选定候选一致，正常 colcon list 无重复包。原 APK 与三份 Nano 源文件备份在 results/optimization_backup，所有原始数据/构建副本排除 Git。

没有声称端到端降低 70–80 ms，也未获得新的光学总延迟；缓存收益需与总显示延迟区分。最终安装后的自然播放验收、具体数据目录和未覆盖场景见 test/camera_delay/OPTIMIZATION.md 与 OPTIMIZATION_SUMMARY.json。未提交或推送 Git。
'''
    record.write_text(text)
print('Updated documentation for ' + args.platform)
