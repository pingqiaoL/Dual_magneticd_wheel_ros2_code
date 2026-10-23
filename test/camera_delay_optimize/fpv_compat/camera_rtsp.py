"""Test-only ROS I420 -> single-slice H264 RTSP for SIYI FPV compatibility."""
import threading,time
import gi
gi.require_version('Gst','1.0')
gi.require_version('GstRtspServer','1.0')
from gi.repository import Gst,GstRtspServer,GLib
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image
Gst.init(None)
rclpy.init()
node=Node('fpv_compat_test')
latest=None
lock=threading.Lock()
def receive(msg):
    global latest
    if msg.encoding != 'yuv420' or msg.width != 1920 or msg.height != 1080:
        return
    with lock:
        latest=msg
node.create_subscription(Image,'/camera/image_yuv',receive,qos_profile_sensor_data)
threading.Thread(target=rclpy.spin,args=(node,),daemon=True).start()
server=GstRtspServer.RTSPServer()
server.set_service('18555')
factory=GstRtspServer.RTSPMediaFactory()
factory.set_shared(True)
factory.set_launch('( appsrc name=frames is-live=true format=time block=false max-buffers=1 max-bytes=0 max-time=0 leaky-type=downstream caps=video/x-raw,format=I420,width=1920,height=1080,framerate=30/1 ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=6000 key-int-max=10 bframes=0 threads=1 sliced-threads=false ! video/x-h264,profile=baseline ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! rtph264pay name=pay0 pt=96 config-interval=-1 )')
def configure(factory,media):
    appsrc=media.get_element().get_by_name('frames')
    start=time.monotonic_ns()
    count=0
    def push():
        nonlocal count
        global latest
        with lock:
            msg=latest
            latest=None
        if msg is None:
            return True
        buf=Gst.Buffer.new_allocate(None,len(msg.data),None)
        buf.fill(0,bytes(msg.data))
        buf.pts=time.monotonic_ns()-start
        buf.dts=buf.pts
        buf.duration=Gst.SECOND//30
        ret=appsrc.emit('push-buffer',buf)
        count+=1
        if count%150==0: print('PUSHED',count,'FLOW',ret,flush=True)
        return ret==Gst.FlowReturn.OK
    timer=GLib.timeout_add(33,push)
    media.connect('unprepared',lambda *_: GLib.source_remove(timer))
factory.connect('media-configure',configure)
server.get_mount_points().add_factory('/live',factory)
server.attach(None)
print('REAL_CAMERA_FP V_TEST_18555',flush=True)
GLib.MainLoop().run()
