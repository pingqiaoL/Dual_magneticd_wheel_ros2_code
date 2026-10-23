import gi
gi.require_version('Gst','1.0')
gi.require_version('GstRtspServer','1.0')
from gi.repository import Gst,GstRtspServer,GLib
Gst.init(None)
server=GstRtspServer.RTSPServer()
server.set_service('18555')
factory=GstRtspServer.RTSPMediaFactory()
factory.set_shared(True)
factory.set_launch('( videotestsrc is-live=true pattern=smpte ! video/x-raw,width=1920,height=1080,framerate=30/1 ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=6000 key-int-max=10 bframes=0 threads=1 sliced-threads=false byte-stream=true ! video/x-h264,profile=baseline ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! rtph264pay name=pay0 pt=96 config-interval=-1 )')
server.get_mount_points().add_factory('/live',factory)
server.attach(None)
print('SYNTHETIC_RTSP_18555',flush=True)
GLib.MainLoop().run()
