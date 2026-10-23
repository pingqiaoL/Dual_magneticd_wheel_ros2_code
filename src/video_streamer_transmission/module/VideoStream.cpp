#include "video_streamer_transmission/VideoStream.hpp"
#include "video_streamer_transmission/VideoPipeline.hpp"

#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/video/video.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace video_streamer_transmission
{
namespace
{
using SteadyClock = std::chrono::steady_clock;
struct SharedState
{
    ~SharedState()
    {
        if (source)
            gst_object_unref(source);
    }
    std::mutex mutex;
    GstElement *source = nullptr;
    VideoStatus status;
    SteadyClock::time_point last_encoded_at{};
};
using StateOwner = std::shared_ptr<SharedState>;

void delete_state(gpointer data, GClosure *)
{
    delete static_cast<StateOwner *>(data);
}

void media_unprepared(GstRTSPMedia *media, gpointer data)
{
    const auto state = *static_cast<StateOwner *>(data);
    GstElement *element = gst_rtsp_media_get_element(media);
    GstElement *source = gst_bin_get_by_name_recurse_up(GST_BIN(element), "frames");
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->source == source)
        {
            gst_object_unref(state->source);
            state->source = nullptr;
        }
    }
    if (source)
        gst_object_unref(source);
    gst_object_unref(element);
}

GstPadProbeReturn count_encoded(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
    {
        const auto state = *static_cast<StateOwner *>(data);
        std::lock_guard<std::mutex> guard(state->mutex);
        ++state->status.encoded_frames;
        state->last_encoded_at = SteadyClock::now();
    }
    return GST_PAD_PROBE_OK;
}

void configure_media(GstRTSPMediaFactory *, GstRTSPMedia *media, gpointer data)
{
    const auto state = *static_cast<StateOwner *>(data);
    GstElement *element = gst_rtsp_media_get_element(media);
    GstElement *source = gst_bin_get_by_name_recurse_up(GST_BIN(element), "frames");
    GstElement *encoded = gst_bin_get_by_name_recurse_up(GST_BIN(element), "encoded");
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->source)
            gst_object_unref(state->source);
        state->source = source;
        state->last_encoded_at = {};
    }
    if (encoded)
    {
        GstPad *pad = gst_element_get_static_pad(encoded, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, count_encoded, new StateOwner(state),
                          [](gpointer owner) { delete static_cast<StateOwner *>(owner); });
        gst_object_unref(pad);
        gst_object_unref(encoded);
    }
    g_signal_connect_data(media, "unprepared", G_CALLBACK(media_unprepared), new StateOwner(state),
                          delete_state, GConnectFlags(0));
    gst_object_unref(element);
}

void client_closed(GstRTSPClient *, gpointer data)
{
    const auto state = *static_cast<StateOwner *>(data);
    std::lock_guard<std::mutex> guard(state->mutex);
    if (state->status.client_count > 0)
        --state->status.client_count;
}

void client_connected(GstRTSPServer *, GstRTSPClient *client, gpointer data)
{
    const auto state = *static_cast<StateOwner *>(data);
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        ++state->status.client_count;
    }
    g_signal_connect_data(client, "closed", G_CALLBACK(client_closed), new StateOwner(state),
                          delete_state, GConnectFlags(0));
}
} // namespace

struct VideoStream::Implementation
{
    explicit Implementation(VideoConfig value) : config(std::move(value))
    {
        VideoPipeline::validate_config(config);
        gst_init(nullptr, nullptr);
        for (const char *name :
             {"appsrc", "queue", "videoconvert", "x264enc", "h264parse", "rtph264pay"})
        {
            GstElementFactory *factory = gst_element_factory_find(name);
            if (!factory)
                throw std::runtime_error(std::string("missing GStreamer plugin: ") + name);
            gst_object_unref(factory);
        }
    }

    void run()
    {
        GMainContext *context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GstRTSPServer *server = gst_rtsp_server_new();
        gst_rtsp_server_set_service(server, std::to_string(config.rtsp_port).c_str());
        GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
        gst_rtsp_media_factory_set_launch(factory,
                                          VideoPipeline::build_description(config).c_str());
        gst_rtsp_media_factory_set_shared(factory, true);
        gst_rtsp_media_factory_set_suspend_mode(factory, GST_RTSP_SUSPEND_MODE_NONE);
        g_signal_connect_data(factory, "media-configure", G_CALLBACK(configure_media),
                              new StateOwner(state), delete_state, GConnectFlags(0));
        g_signal_connect_data(server, "client-connected", G_CALLBACK(client_connected),
                              new StateOwner(state), delete_state, GConnectFlags(0));
        GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
        gst_rtsp_mount_points_add_factory(mounts, config.stream_path.c_str(), factory);
        g_object_unref(mounts);
        GError *error = nullptr;
        GSource *listener = gst_rtsp_server_create_source(server, nullptr, &error);
        {
            std::lock_guard<std::mutex> guard(state->mutex);
            state->status.state = listener ? "LISTENING" : "ERROR";
            state->status.last_error = error ? error->message : "";
        }
        g_clear_error(&error);
        {
            std::lock_guard<std::mutex> guard(queue_mutex);
            startup_complete = true;
        }
        wake.notify_all();
        if (listener)
            g_source_attach(listener, context);
        auto next_frame_at = SteadyClock::now();
        while (running && listener)
        {
            for (int iteration = 0; iteration < 16 && g_main_context_pending(context); ++iteration)
            {
                g_main_context_iteration(context, false);
            }
            if (SteadyClock::now() >= next_frame_at)
            {
                std::optional<VideoFrame> frame;
                SteadyClock::time_point received_at;
                {
                    std::lock_guard<std::mutex> guard(queue_mutex);
                    frame = std::move(pending_frame);
                    pending_frame.reset();
                    received_at = pending_at;
                }
                if (frame)
                {
                    send_frame(std::move(*frame), received_at);
                    next_frame_at = std::max(
                        SteadyClock::now(),
                        next_frame_at + std::chrono::nanoseconds(1000000000 / config.frame_rate));
                }
            }
            std::unique_lock<std::mutex> guard(queue_mutex);
            wake.wait_for(guard, std::chrono::milliseconds(5), [this] { return !running; });
        }
        if (listener)
        {
            g_source_destroy(listener);
            g_source_unref(listener);
        }
        // 主动关闭客户端和会话，确保停止服务后编码器与监听端口释放。
        GList *clients = gst_rtsp_server_client_filter(
            server, [](GstRTSPServer *, GstRTSPClient *, gpointer) { return GST_RTSP_FILTER_REF; },
            nullptr);
        for (GList *item = clients; item; item = item->next)
        {
            gst_rtsp_client_close(GST_RTSP_CLIENT(item->data));
            g_object_unref(item->data);
        }
        g_list_free(clients);
        GstRTSPSessionPool *pool = gst_rtsp_server_get_session_pool(server);
        gst_rtsp_session_pool_filter(
            pool, [](GstRTSPSessionPool *, GstRTSPSession *, gpointer)
            { return GST_RTSP_FILTER_REMOVE; }, nullptr);
        g_object_unref(pool);
        g_object_unref(server);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->source)
        {
            gst_object_unref(state->source);
            state->source = nullptr;
        }
        state->status.client_count = 0;
    }

    void send_frame(VideoFrame frame, SteadyClock::time_point received_at)
    {
        GstElement *source = nullptr;
        {
            std::lock_guard<std::mutex> guard(state->mutex);
            if (state->source)
                source = GST_ELEMENT(gst_object_ref(state->source));
            if (!source || SteadyClock::now() - received_at >
                               std::chrono::milliseconds(config.max_frame_age_ms))
            {
                ++state->status.dropped_frames;
                if (source)
                    gst_object_unref(source);
                return;
            }
        }
        // 只复制一次到 GStreamer；ROS 回调不承担内存复制和编码工作。
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, frame.size_bytes, nullptr);
        gst_buffer_fill(buffer, 0, frame.pixels, frame.size_bytes);
        const gsize y_size = static_cast<gsize>(config.width) * config.height;
        gsize offsets[GST_VIDEO_MAX_PLANES]{0, y_size, y_size + y_size / 4, 0};
        gint strides[GST_VIDEO_MAX_PLANES]{config.width, config.width / 2, config.width / 2, 0};
        gst_buffer_add_video_meta_full(buffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_I420,
                                       config.width, config.height, 3, offsets, strides);
        GstClock *clock = gst_element_get_clock(source);
        const auto base_time = gst_element_get_base_time(source);
        const auto current_time = clock ? gst_clock_get_time(clock) : GST_CLOCK_TIME_NONE;
        GST_BUFFER_PTS(buffer) = clock && current_time >= base_time ? current_time - base_time : 0;
        GST_BUFFER_DURATION(buffer) = GST_SECOND / config.frame_rate;
        if (clock)
            gst_object_unref(clock);
        const auto result = gst_app_src_push_buffer(GST_APP_SRC(source), buffer);
        gst_object_unref(source);
        std::lock_guard<std::mutex> guard(state->mutex);
        if (result == GST_FLOW_OK)
        {
            ++state->status.submitted_frames;
            state->status.last_error.clear();
        }
        else
        {
            ++state->status.dropped_frames;
            state->status.last_error = gst_flow_get_name(result);
        }
    }

    VideoConfig config;
    StateOwner state = std::make_shared<SharedState>();
    std::atomic<bool> running{false};
    std::thread worker;
    std::mutex queue_mutex;
    std::condition_variable wake;
    std::optional<VideoFrame> pending_frame;
    SteadyClock::time_point pending_at{};
    bool startup_complete = false;
};

VideoStream::VideoStream(VideoConfig config)
    : _implementation(std::make_unique<Implementation>(std::move(config)))
{
}

VideoStream::~VideoStream()
{
    stop();
}

bool VideoStream::start()
{
    auto &implementation = *_implementation;
    if (implementation.running)
    {
        if (status().state != "ERROR")
            return true;
        stop();
    }
    if (implementation.worker.joinable())
        implementation.worker.join();
    implementation.startup_complete = false;
    implementation.running = true;
    implementation.worker = std::thread([&implementation] { implementation.run(); });
    std::unique_lock<std::mutex> guard(implementation.queue_mutex);
    const bool ready =
        implementation.wake.wait_for(guard, std::chrono::seconds(3),
                                     [&implementation] { return implementation.startup_complete; });
    return ready && status().state != "ERROR";
}

void VideoStream::stop()
{
    auto &implementation = *_implementation;
    implementation.running = false;
    implementation.wake.notify_all();
    if (implementation.worker.joinable())
        implementation.worker.join();
    {
        std::lock_guard<std::mutex> guard(implementation.queue_mutex);
        implementation.pending_frame.reset();
    }
    std::lock_guard<std::mutex> guard(implementation.state->mutex);
    implementation.state->status.state = "STOPPED";
}

void VideoStream::submit_frame(VideoFrame frame)
{
    auto &implementation = *_implementation;
    if (!implementation.running || !frame.owner || !frame.pixels ||
        frame.size_bytes != static_cast<std::size_t>(implementation.config.width) *
                                implementation.config.height * 3 / 2)
    {
        return;
    }
    std::lock_guard<std::mutex> queue_guard(implementation.queue_mutex);
    std::lock_guard<std::mutex> status_guard(implementation.state->mutex);
    ++implementation.state->status.received_frames;
    if (implementation.pending_frame)
        ++implementation.state->status.dropped_frames;
    implementation.pending_frame = std::move(frame);
    implementation.pending_at = SteadyClock::now();
}

VideoStatus VideoStream::status() const
{
    const auto &implementation = *_implementation;
    std::lock_guard<std::mutex> guard(implementation.state->mutex);
    auto result = implementation.state->status;
    if (result.state != "STOPPED" && result.state != "ERROR")
    {
        result.state = result.client_count == 0 ? "LISTENING"
                       : SteadyClock::now() - implementation.state->last_encoded_at <
                               std::chrono::milliseconds(implementation.config.frame_timeout_ms)
                           ? "STREAMING"
                           : "WAITING_FRAME";
    }
    return result;
}
} // namespace video_streamer_transmission
