#!/usr/bin/env python3
"""Add RTP frame identifiers only to an already instrumented test copy."""

import argparse
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument('--runtime-root', type=Path, required=True)
arguments = parser.parse_args()
path = arguments.runtime_root / 'src/video_streamer_transmission/module/VideoStream.cpp'
text = path.read_text()
if 'record_rtp_frame' in text:
    raise SystemExit('RTP trace already added; no change')
marker = 'void configure_media(GstRTSPMediaFactory *, GstRTSPMedia *media, gpointer data)'
assert text.count(marker) == 1
helper = '''struct RtpTraceState
{
    std::mutex mutex;
    std::uint64_t previous_key = 0;
    bool has_previous = false;
};

void record_rtp_frame(GstBuffer *buffer, RtpTraceState &state)
{
    std::uint8_t header[12]{};
    if (gst_buffer_extract(buffer, 0, header, sizeof(header)) != sizeof(header))
        return;
    const auto timestamp = (std::uint32_t(header[4]) << 24) | (std::uint32_t(header[5]) << 16) |
                           (std::uint32_t(header[6]) << 8) | header[7];
    const auto source = (std::uint32_t(header[8]) << 24) | (std::uint32_t(header[9]) << 16) |
                        (std::uint32_t(header[10]) << 8) | header[11];
    const auto key = (std::uint64_t(source) << 32) | timestamp;
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.has_previous || state.previous_key != key)
    {
        camera_delay_test::record("rtp_first", key, GST_BUFFER_PTS(buffer));
        state.previous_key = key;
        state.has_previous = true;
    }
    if (header[1] & 0x80)
        camera_delay_test::record("rtp_end", key, GST_BUFFER_PTS(buffer));
}

GstPadProbeReturn trace_rtp(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto &state = *static_cast<RtpTraceState *>(data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
        record_rtp_frame(GST_PAD_PROBE_INFO_BUFFER(info), state);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST)
    {
        auto *buffers = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        for (guint index = 0; index < gst_buffer_list_length(buffers); ++index)
            record_rtp_frame(gst_buffer_list_get(buffers, index), state);
    }
    return GST_PAD_PROBE_OK;
}

'''
text = text.replace(marker, helper + marker)
marker = '    GstElement *encoded = gst_bin_get_by_name_recurse_up(GST_BIN(element), "encoded");'
assert text.count(marker) == 1
text = text.replace(marker, marker + '''
    GstElement *payloader = gst_bin_get_by_name_recurse_up(GST_BIN(element), "pay0");
    if (payloader)
    {
        GstPad *output_pad = gst_element_get_static_pad(payloader, "src");
        gst_pad_add_probe(output_pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER |
                          GST_PAD_PROBE_TYPE_BUFFER_LIST), trace_rtp, new RtpTraceState,
                          [](gpointer state) { delete static_cast<RtpTraceState *>(state); });
        gst_object_unref(output_pad);
        gst_object_unref(payloader);
    }''')
path.write_text(text)
print(path)
