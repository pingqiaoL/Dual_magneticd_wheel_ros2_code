#!/usr/bin/env python3
"""Copy current camera/video packages and instrument only the isolated copies."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path


VISION_PROBE_CMAKE = r'''cmake_minimum_required(VERSION 3.16)
project(camera_delay_vision_probe)
find_package(ament_cmake REQUIRED)
find_package(OpenCV REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
add_library(vision_probe_component SHARED src/VisionProbeNode.cpp)
target_compile_features(vision_probe_component PUBLIC cxx_std_17)
target_include_directories(vision_probe_component PRIVATE ${OpenCV_INCLUDE_DIRS})
ament_target_dependencies(vision_probe_component rclcpp rclcpp_components sensor_msgs)
target_link_libraries(vision_probe_component ${OpenCV_LIBS})
rclcpp_components_register_nodes(vision_probe_component "camera_delay_test::VisionProbeNode")
install(TARGETS vision_probe_component ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
ament_package()
'''


VISION_PROBE_PACKAGE = r'''<?xml version="1.0"?>
<package format="3">
  <name>camera_delay_vision_probe</name>
  <version>0.0.0</version>
  <description>Test-only I420 to BGR vision branch load probe.</description>
  <maintainer email="test@example.invalid">camera delay test</maintainer>
  <license>Apache-2.0</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>rclcpp_components</depend>
  <depend>sensor_msgs</depend>
  <depend>opencv4</depend>
</package>
'''


VISION_PROBE_SOURCE = r'''#include <chrono>
#include <cstdint>
#include <memory>

#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace camera_delay_test
{
class VisionProbeNode final : public rclcpp::Node
{
public:
    explicit VisionProbeNode(const rclcpp::NodeOptions &options)
        : Node("camera_delay_vision_probe", options), _started(std::chrono::steady_clock::now())
    {
        _subscription = create_subscription<sensor_msgs::msg::Image>(
            "/camera/image_raw", rclcpp::SensorDataQoS().keep_last(1),
            [this](sensor_msgs::msg::Image::ConstSharedPtr image)
            {
                if (image->encoding != "yuv420" || image->data.size() !=
                        static_cast<std::size_t>(image->width) * image->height * 3 / 2)
                {
                    return;
                }
                const cv::Mat i420(static_cast<int>(image->height * 3 / 2),
                                   static_cast<int>(image->width), CV_8UC1,
                                   const_cast<std::uint8_t *>(image->data.data()));
                cv::cvtColor(i420, _bgr, cv::COLOR_YUV2BGR_I420);
                ++_frames;
                const auto now = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration<double>(now - _started).count();
                if (elapsed >= 5.0)
                {
                    RCLCPP_INFO(get_logger(), "VISION_BGR fps=%.2f size=%dx%d",
                                _frames / elapsed, _bgr.cols, _bgr.rows);
                    _frames = 0;
                    _started = now;
                }
            });
    }

private:
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr _subscription;
    cv::Mat _bgr;
    std::uint64_t _frames{0};
    std::chrono::steady_clock::time_point _started;
};
}

RCLCPP_COMPONENTS_REGISTER_NODE(camera_delay_test::VisionProbeNode)
'''


def prepare(project_root, runtime_root, pixel_format):
    suite = Path(__file__).resolve().parent
    source_root = runtime_root / 'src'
    source_root.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for package in ['camera_driver', 'video_streamer_transmission']:
        destination = source_root / package
        if destination.exists():
            raise RuntimeError(f'Refusing to overwrite existing test copy: {destination}')
        shutil.copytree(project_root / 'src' / package, destination)
        for source in (project_root / 'src' / package).rglob('*'):
            if source.is_file():
                manifest[str(source.relative_to(project_root))] = hashlib.sha256(source.read_bytes()).hexdigest()
    video_package = source_root / 'video_streamer_transmission'
    (video_package / 'src/DelayVisionProbeNode.cpp').write_text(VISION_PROBE_SOURCE)
    patch_text = (video_package / 'CMakeLists.txt').read_text()
    patch_text = patch_text.replace(
        'find_package(PkgConfig REQUIRED)',
        'find_package(PkgConfig REQUIRED)\nfind_package(OpenCV REQUIRED)',
    ).replace(
        'add_library(video_component SHARED src/VideoStreamerNode.cpp)',
        'add_library(video_component SHARED src/VideoStreamerNode.cpp src/DelayVisionProbeNode.cpp)',
    ).replace(
        'target_link_libraries(video_component video_stream)',
        'target_include_directories(video_component PRIVATE ${OpenCV_INCLUDE_DIRS})\n'
        'target_link_libraries(video_component video_stream ${OpenCV_LIBS})',
    ).replace(
        'rclcpp_components_register_node(video_component PLUGIN "video_streamer_transmission::VideoStreamerNode" EXECUTABLE video_streamer_node)',
        'rclcpp_components_register_node(video_component PLUGIN "video_streamer_transmission::VideoStreamerNode" EXECUTABLE video_streamer_node)\n'
        'rclcpp_components_register_node(video_component PLUGIN "camera_delay_test::VisionProbeNode" EXECUTABLE camera_delay_vision_probe_node)',
    )
    (video_package / 'CMakeLists.txt').write_text(patch_text)
    (runtime_root / 'source_manifest.json').write_text(json.dumps(manifest, indent=2))

    def patch(relative, original, replacement):
        path = source_root / relative
        text = path.read_text()
        if text.count(original) != 1:
            raise RuntimeError(f'Source changed; expected exactly one match: {relative}: {original[:80]}')
        path.write_text(text.replace(original, replacement))

    def include(relative):
        path = source_root / relative
        path.write_text(f'#include "{suite / "DelayTrace.hpp"}"\n' + path.read_text())

    patch('camera_driver/include/camera_driver/CameraTypes.hpp',
          'struct CameraFrame\n{', 'struct CameraFrame\n{\n    std::uint64_t delay_trace_id = 0;')
    include('camera_driver/module/CameraDevice.cpp')
    patch('camera_driver/module/CameraDevice.cpp', 'v4l2src device=',
          'v4l2src name=delay_camera_source device=')
    patch('camera_driver/module/CameraDevice.cpp', 'GstBus *bus = pipeline ? gst_element_get_bus(pipeline) : nullptr;', '''GstBus *bus = pipeline ? gst_element_get_bus(pipeline) : nullptr;
        if (pipeline)
        {
            GstElement *trace_source = gst_bin_get_by_name(GST_BIN(pipeline), "delay_camera_source");
            if (trace_source)
            {
                GstPad *trace_pad = gst_element_get_static_pad(trace_source, "src");
                gst_pad_add_probe(trace_pad, GST_PAD_PROBE_TYPE_BUFFER,
                    [](GstPad *, GstPadProbeInfo *probe, gpointer)
                    {
                        auto *buffer = GST_PAD_PROBE_INFO_BUFFER(probe);
                        camera_delay_test::record("source", GST_BUFFER_PTS(buffer));
                        return GST_PAD_PROBE_OK;
                    }, nullptr, nullptr);
                gst_object_unref(trace_pad);
                gst_object_unref(trace_source);
            }
        }''')
    patch('camera_driver/module/CameraDevice.cpp', 'GstVideoInfo info;', '''camera_delay_test::record("sample", GST_BUFFER_PTS(gst_sample_get_buffer(sample)));
                GstVideoInfo info;''')
    patch('camera_driver/module/CameraDevice.cpp', 'frame->width = GST_VIDEO_INFO_WIDTH(&info);', '''frame->delay_trace_id = GST_BUFFER_PTS(gst_sample_get_buffer(sample));
                frame->width = GST_VIDEO_INFO_WIDTH(&info);''')
    patch('camera_driver/module/CameraDevice.cpp', '_callback(std::move(frame));', '''camera_delay_test::record("copy_complete", frame->delay_trace_id);
                    _callback(std::move(frame));''')
    include('camera_driver/src/CameraCaptureNode.cpp')
    patch('camera_driver/src/CameraCaptureNode.cpp', '_image_publisher->publish(std::move(image));', '''camera_delay_test::record("publish", rclcpp::Time(image->header.stamp).nanoseconds(), frame->delay_trace_id);
    _image_publisher->publish(std::move(image));''')

    patch('video_streamer_transmission/include/video_streamer_transmission/VideoTypes.hpp',
          'struct VideoFrame\n{', 'struct VideoFrame\n{\n    std::uint64_t delay_trace_id = 0;')
    include('video_streamer_transmission/src/VideoStreamerNode.cpp')
    patch('video_streamer_transmission/src/VideoStreamerNode.cpp', '    VideoFrame frame;', '''    camera_delay_test::record("video_receive", rclcpp::Time(image->header.stamp).nanoseconds());
    VideoFrame frame;
    frame.delay_trace_id = rclcpp::Time(image->header.stamp).nanoseconds();''')
    patch('video_streamer_transmission/module/VideoPipeline.cpp', 'x264enc tune=', 'x264enc name=delay_encoder tune=')
    patch('video_streamer_transmission/module/VideoPipeline.cpp',
          '#include <sstream>\n', '#include <cstdlib>\n#include <sstream>\n')
    patch('video_streamer_transmission/module/VideoPipeline.cpp',
          '    validate_config(config);\n    std::ostringstream description;', '''    validate_config(config);
    const char *test_encoder_options = std::getenv("CAMERA_DELAY_X264_OPTIONS");
    const std::string encoder_options = test_encoder_options && *test_encoder_options
                                            ? test_encoder_options
                                            : "speed-preset=veryfast";
    std::ostringstream description;''')
    patch('video_streamer_transmission/module/VideoPipeline.cpp',
          '"x264enc name=delay_encoder tune=zerolatency speed-preset=veryfast bframes=0 "\n                   "bitrate="',
          '"x264enc name=delay_encoder tune=zerolatency bframes=0 "\n                << encoder_options << " bitrate="')
    include('video_streamer_transmission/module/VideoStream.cpp')
    patch('video_streamer_transmission/module/VideoStream.cpp', '        ++state->status.encoded_frames;', '''        camera_delay_test::record("encoded", GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info)));
        ++state->status.encoded_frames;''')
    patch('video_streamer_transmission/module/VideoStream.cpp', '    if (encoded)\n', '''    GstElement *encoder = gst_bin_get_by_name_recurse_up(GST_BIN(element), "delay_encoder");
    if (encoder)
    {
        GstPad *input_pad = gst_element_get_static_pad(encoder, "sink");
        gst_pad_add_probe(input_pad, GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad *, GstPadProbeInfo *probe, gpointer)
            {
                camera_delay_test::record("encoder_input", GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(probe)));
                return GST_PAD_PROBE_OK;
            }, nullptr, nullptr);
        gst_object_unref(input_pad);
        gst_object_unref(encoder);
    }
    if (encoded)
''')
    patch('video_streamer_transmission/module/VideoStream.cpp', '        const auto result = gst_app_src_push_buffer(GST_APP_SRC(source), buffer);', '''        camera_delay_test::record("push", frame.delay_trace_id, GST_BUFFER_PTS(buffer));
        const auto result = gst_app_src_push_buffer(GST_APP_SRC(source), buffer);''')
    if pixel_format == 'I420':
        patch('camera_driver/module/CameraDevice.cpp',
              'videoconvert ! video/x-raw,format=BGR ! ',
              'videoconvert ! video/x-raw,format=I420 ! ')
        patch('camera_driver/module/CameraDevice.cpp',
              'GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_BGR',
              'GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420')
        patch('camera_driver/module/CameraDevice.cpp',
              'reason = "invalid BGR camera sample";',
              'reason = "invalid I420 camera sample";')
        patch('camera_driver/module/CameraDevice.cpp', '''const std::size_t row_bytes = static_cast<std::size_t>(frame->width) * 3;
                frame->pixels.resize(row_bytes * frame->height);
                const auto *pixels =
                    static_cast<const std::uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&video_frame, 0));
                const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&video_frame, 0);
                for (std::uint32_t row = 0; row < frame->height; ++row)
                {
                    std::memcpy(frame->pixels.data() + row * row_bytes, pixels + row * stride,
                                row_bytes);
                }''', '''const std::size_t y_size = static_cast<std::size_t>(frame->width) * frame->height;
                const std::size_t chroma_width = frame->width / 2;
                const std::size_t chroma_height = frame->height / 2;
                const std::size_t chroma_size = chroma_width * chroma_height;
                frame->pixels.resize(y_size + 2 * chroma_size);
                const std::size_t plane_offsets[3]{0, y_size, y_size + chroma_size};
                const std::size_t row_bytes[3]{frame->width, chroma_width, chroma_width};
                const std::size_t rows[3]{frame->height, chroma_height, chroma_height};
                for (std::size_t plane = 0; plane < 3; ++plane)
                {
                    const auto *pixels = static_cast<const std::uint8_t *>(
                        GST_VIDEO_FRAME_PLANE_DATA(&video_frame, plane));
                    const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&video_frame, plane);
                    for (std::size_t row = 0; row < rows[plane]; ++row)
                    {
                        std::memcpy(frame->pixels.data() + plane_offsets[plane] + row * row_bytes[plane],
                                    pixels + row * stride, row_bytes[plane]);
                    }
                }''')
        patch('camera_driver/src/CameraCaptureNode.cpp',
              'image->encoding = "bgr8";\n    image->step = frame->width * 3;',
              'image->encoding = "yuv420";\n    image->step = frame->width;')
        patch('video_streamer_transmission/src/VideoStreamerNode.cpp', '''if (image->encoding != "bgr8" || image->width != static_cast<std::uint32_t>(_config.width) ||
        image->height != static_cast<std::uint32_t>(_config.height) ||
        image->step != image->width * 3 ||
        image->data.size() != static_cast<std::size_t>(image->step) * image->height)''', '''if (image->encoding != "yuv420" || image->width != static_cast<std::uint32_t>(_config.width) ||
        image->height != static_cast<std::uint32_t>(_config.height) ||
        image->step != image->width ||
        image->data.size() != static_cast<std::size_t>(image->width) * image->height * 3 / 2)''')
        patch('video_streamer_transmission/module/VideoPipeline.cpp',
              'caps=video/x-raw,format=BGR,width=',
              'caps=video/x-raw,format=I420,width=')
        patch('video_streamer_transmission/module/VideoPipeline.cpp',
              '"videoconvert ! video/x-raw,format=I420 ! "\n                   ', '')
        patch('video_streamer_transmission/module/VideoStream.cpp', '''frame.size_bytes != static_cast<std::size_t>(implementation.config.width) *
                                implementation.config.height * 3''', '''frame.size_bytes != static_cast<std::size_t>(implementation.config.width) *
                                implementation.config.height * 3 / 2''')
        patch('video_streamer_transmission/module/VideoStream.cpp', '''gsize offsets[GST_VIDEO_MAX_PLANES]{};
        gint strides[GST_VIDEO_MAX_PLANES]{config.width * 3, 0, 0, 0};
        gst_buffer_add_video_meta_full(buffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_BGR,
                                       config.width, config.height, 1, offsets, strides);''', '''const gsize y_size = static_cast<gsize>(config.width) * config.height;
        const gsize chroma_size = y_size / 4;
        gsize offsets[GST_VIDEO_MAX_PLANES]{0, y_size, y_size + chroma_size, 0};
        gint strides[GST_VIDEO_MAX_PLANES]{config.width, config.width / 2, config.width / 2, 0};
        gst_buffer_add_video_meta_full(buffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_I420,
                                       config.width, config.height, 3, offsets, strides);''')
    print(runtime_root, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project-root', type=Path, default=Path('/workspace'))
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--pixel-format', choices=('BGR', 'I420'), default='BGR')
    arguments = parser.parse_args()
    prepare(arguments.project_root, arguments.runtime_root, arguments.pixel_format)
