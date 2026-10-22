#!/usr/bin/env python3
"""Copy current camera/video packages and instrument only the isolated copies."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path


def prepare(project_root, runtime_root):
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
    print(runtime_root, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project-root', type=Path, default=Path('/workspace'))
    parser.add_argument('--runtime-root', type=Path, required=True)
    arguments = parser.parse_args()
    prepare(arguments.project_root, arguments.runtime_root)
