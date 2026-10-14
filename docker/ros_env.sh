# 供交互终端、非交互 bash 和容器入口加载 ROS 环境。
source /opt/ros/jazzy/setup.bash
export VIRTUAL_ENV=/opt/robot-venv
case ":${PATH}:" in
  *:/opt/robot-venv/bin:*) ;;
  *) export PATH="/opt/robot-venv/bin:${PATH}" ;;
esac
if [[ -f /workspace/install/setup.bash ]]; then
  source /workspace/install/setup.bash
fi
