#!/usr/bin/env python3
"""不使用 pyserial，不主动清空缓存的 POSIX 原始采集，保存 RX 与元数据。"""
import argparse, datetime, json, os, select, termios, time
from pathlib import Path
from usb_protocol_lab import describe
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='/dev/ttyACM0'); p.add_argument('--duration',type=float,default=30)
p.add_argument('--out',default='/workspace/test/results/raw_no_flush')
a=p.parse_args()
if not 0<a.duration<=60: p.error('duration must be 0..60')
info=describe(a.port)
if (info.get('idVendor'),info.get('idProduct'))!=('28e9','018a'): raise RuntimeError('target mismatch')
out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
fd=os.open(a.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK|os.O_CLOEXEC)
old=termios.tcgetattr(fd); attrs=termios.tcgetattr(fd)
attrs[0]=0; attrs[1]=0; attrs[3]=0
attrs[2]=(attrs[2]&~(termios.CSIZE|termios.PARENB|termios.CSTOPB|termios.CRTSCTS))|termios.CS8|termios.CLOCAL|termios.CREAD
attrs[4]=attrs[5]=termios.B115200
attrs[6][termios.VMIN]=0; attrs[6][termios.VTIME]=0
start=time.monotonic(); events=[]; error=None
info.update(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),action='raw-capture',baud=115200,flush_input=False)
try:
    termios.tcsetattr(fd,termios.TCSANOW,attrs)
    while time.monotonic()-start<a.duration:
        ready,_,_=select.select([fd],[],[],.1)
        if ready:
            try: data=os.read(fd,4096)
            except BlockingIOError: continue
            if not data: raise OSError('EOF or disconnect')
            event={'elapsed_s':round(time.monotonic()-start,6),'direction':'RX','length':len(data),'hex':data.hex(' ')}
            events.append(event); print(json.dumps(event),flush=True)
except BaseException as exc:
    error=str(exc); raise
finally:
    try: termios.tcsetattr(fd,termios.TCSANOW,old)
    finally: os.close(fd)
    raw=b''.join(bytes.fromhex(e['hex']) for e in events)
    info.update(duration_s=round(time.monotonic()-start,3),rx_bytes=len(raw),tx_bytes=0,error=error)
    (out/'rx.bin').write_bytes(raw); (out/'tx.bin').write_bytes(b'')
    (out/'capture.json').write_text(json.dumps({'metadata':info,'events':events},ensure_ascii=False,indent=2))
    print('SAVED',out,'RX',len(raw),'TX 0')
