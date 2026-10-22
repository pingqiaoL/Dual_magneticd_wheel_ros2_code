ARG ROS_BASE_IMAGE=public.ecr.aws/docker/library/ros:jazzy-ros-base-noble@sha256:ec00a06df4f573a86e4336c4585940f7f614646ef22675ace1b3e14ee0a82cb3
FROM ${ROS_BASE_IMAGE}

ARG USER_UID=1000
ARG USER_GID=1000
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      python3-opencv python3-pip python3-serial python3-venv \
      ros-jazzy-rmw-cyclonedds-cpp \
      ros-jazzy-cv-bridge \
      ros-jazzy-demo-nodes-cpp ros-jazzy-demo-nodes-py \
      libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstrtspserver-1.0-dev \
      gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
      gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
      v4l-utils python3-gi gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 \
    && rm -rf /var/lib/apt/lists/*

COPY docker/requirements-dev.txt /tmp/requirements-dev.txt
ARG PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple
RUN python3 -m venv --system-site-packages /opt/robot-venv \
    && /opt/robot-venv/bin/pip install --no-cache-dir --index-url "${PIP_INDEX_URL}" -r /tmp/requirements-dev.txt

# Noble 镜像可能已有 uid=1000 的用户；复用其 UID，避免工作区产生 root 文件。
RUN test "${USER_UID}" -gt 0 && test "${USER_GID}" -gt 0 \
    && (getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" robot) \
    && if getent passwd "${USER_UID}" >/dev/null; then \
         previous_user="$(getent passwd "${USER_UID}" | cut -d: -f1)"; \
         usermod --login robot --home /home/robot --move-home "${previous_user}"; \
         usermod --gid "${USER_GID}" robot; \
       else \
         useradd --uid "${USER_UID}" --gid "${USER_GID}" --create-home --shell /bin/bash robot; \
       fi \
    && mkdir -p /workspace /home/robot/.ros \
    && chown -R "${USER_UID}:${USER_GID}" /workspace /home/robot \
    && printf '\nsource /usr/local/share/robot_ros2_env.sh\n' >> /home/robot/.bashrc

COPY --chmod=644 docker/ros_env.sh /usr/local/share/robot_ros2_env.sh
COPY --chmod=755 docker/entrypoint.sh /usr/local/bin/robot-entrypoint

ENV HOME=/home/robot \
    PATH=/opt/robot-venv/bin:${PATH} \
    BASH_ENV=/usr/local/share/robot_ros2_env.sh \
    PYTHONUNBUFFERED=1

USER robot
WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/robot-entrypoint"]
CMD ["sleep", "infinity"]
