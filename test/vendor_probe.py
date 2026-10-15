#!/usr/bin/env python3
"""仅原样发送厂家提供的广播和心跳请求，不修改字段，不发送配置命令。"""
import argparse
from usb_protocol_lab import Session, HEAD, TAIL
BROADCAST=bytes.fromhex('FE E1 90 0D 00 01 00 00 FF FF FF 00 00 00 00 00 00 00 00 00 00 0C 45 4D 32 33 31 31 30 38 43 30 30 36 2B 21 0D 90 E1 FE')
HEARTBEAT=bytes.fromhex('FE E1 90 0D 00 01 00 00 FF FF 01 00 00 00 00 00 00 00 00 00 00 0C 45 4D 32 33 31 31 30 38 43 30 30 36 2B 21 0D 90 E1 FE')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    
    p.add_argument('--port',default='/dev/ttyACM0');p.add_argument('--baud',type=int,default=115200)
    p.add_argument('--duration',type=float,default=30);p.add_argument('--out',default='/workspace/test/results/vendor_115200')
    a=p.parse_args();a.action='vendor-exact-requests'
    if not 0<a.duration<=60:p.error('duration 0..60')
    for frame in (BROADCAST,HEARTBEAT):
     assert len(frame)==40 and frame[:4]==HEAD and frame[-4:]==TAIL and frame[5]==1
    s=Session(a)
    try:
     s.capture(1,'before_vendor')
     for frame,case in [(BROADCAST,'vendor_broadcast'),(HEARTBEAT,'vendor_heartbeat')]:
      if s.ser.write(frame)!=len(frame):raise RuntimeError('partial write')
      s.ser.flush();s.event('TX',frame,case);s.capture(2,case)
     s.capture(a.duration,'after_vendor')
    except BaseException as exc:s.close(str(exc));raise
    else:s.close()

if __name__ == "__main__":
    main()
