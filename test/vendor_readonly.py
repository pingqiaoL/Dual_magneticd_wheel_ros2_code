#!/usr/bin/env python3
"""厂家请求复测与有证据支持的CRC规则下的只读查询。"""
import argparse,time
from usb_protocol_lab import Session,HEAD,TAIL,crc16
from vendor_probe import BROADCAST,HEARTBEAT

def query(category,payload):
 if category not in (1,2,6):raise ValueError('read-only category whitelist')
 body=b'\x00\x01\x00\x00\x01\x03'+bytes([category])+bytes(9)+len(payload).to_bytes(2,'big')+payload
 return HEAD+body+crc16(payload,'MODBUS').to_bytes(2,'big')+TAIL

def send(s,frame,label):
 if frame[5]!=1:raise ValueError('read-only function required')
 if s.ser.write(frame)!=len(frame):raise RuntimeError('partial write')
 s.ser.flush();s.event('TX',frame,label);s.capture(.5,label)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    
    p.add_argument('--port',default='/dev/ttyACM0');p.add_argument('--baud',type=int,default=115200)
    p.add_argument('--duration',type=float,default=50);p.add_argument('--out',default='/workspace/test/results/vendor_live')
    a=p.parse_args();a.action='verified-crc-readonly'
    if not 0<a.duration<=60:p.error('duration 0..60')
    s=Session(a)
    try:
     s.capture(.5,'before')
     send(s,BROADCAST,'vendor_broadcast')
     send(s,HEARTBEAT,'vendor_heartbeat')
     send(s,query(2,b'CT260918B002'),'state_12_device_SN')
     send(s,query(2,b'CT260918B002'+bytes(8)),'state_20_device_SN')
     end=time.monotonic()+a.duration
     while time.monotonic()<end:
      send(s,HEARTBEAT,'heartbeat_keepalive')
      s.capture(.5,'measurement_wait')
    except BaseException as exc:s.close(str(exc));raise
    else:s.close()

if __name__=="__main__":
    main()
