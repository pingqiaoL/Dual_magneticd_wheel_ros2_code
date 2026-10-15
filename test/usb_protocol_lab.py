#!/usr/bin/env python3
"""只读协议实验：描述设备、被动捕获、限定查询、离线分析。禁止配置/校准写入。"""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import struct
import sys
import time

HEAD = bytes.fromhex('fe e1 90 0d')
TAIL = bytes.fromhex('0d 90 e1 fe')
CRC_SPECS = {
    'MODBUS': (0xA001, 0xFFFF, 0, True),
    'ARC': (0xA001, 0, 0, True),
    'CCITT_FALSE': (0x1021, 0xFFFF, 0, False),
    'XMODEM': (0x1021, 0, 0, False),
    'KERMIT': (0x8408, 0, 0, True),
    'X25': (0x8408, 0xFFFF, 0xFFFF, True),
    'USB': (0xA001, 0xFFFF, 0xFFFF, True),
    'DNP': (0xA6BC, 0, 0xFFFF, True),
}


def crc16(data, name):
    poly, value, xorout, reflected = CRC_SPECS[name]
    for byte in data:
        value ^= byte if reflected else byte << 8
        for _ in range(8):
            if reflected:
                value = (value >> 1) ^ (poly if value & 1 else 0)
            else:
                value = ((value << 1) ^ (poly if value & 0x8000 else 0)) & 0xFFFF
    return value ^ xorout


def selftest():
    checks = dict(MODBUS=0x4B37, ARC=0xBB3D, CCITT_FALSE=0x29B1,
                  XMODEM=0x31C3, KERMIT=0x2189, X25=0x906E, USB=0xB4C8, DNP=0xEA82)
    for name, expected in checks.items():
        assert crc16(b'123456789', name) == expected, name
    frame = usb_query(2, b'0'*12, 1, 'MODBUS', 'body', 'little')
    assert len(frame) == 40 and frame[5] == 1 and frame[10] == 2
    assert int.from_bytes(frame[20:22], 'big') == 12
    print('PASS 8 CRC check vectors and read-only frame construction')


def usb_query(category, payload, transaction, crc_name, scope, byte_order):
    # 只允许原始 USB 文档中定义的读取状态、读取系数请求。
    if category not in (2, 6) or len(payload) not in (12, 20):
        raise ValueError('read-only query whitelist rejected request')
    body = (b'\x00\x01' + transaction.to_bytes(2, 'big') +
            bytes([1, 3, category]) + bytes(7) + bytes(2) +
            len(payload).to_bytes(2, 'big') + payload)
    covered = body if scope == 'body' else HEAD + body
    crc = crc16(covered, crc_name).to_bytes(2, byte_order)
    return HEAD + body + crc + TAIL


def describe(port):
    name = Path(port).resolve().name
    p = (Path('/sys/class/tty') / name / 'device').resolve()
    result = {'port': port, 'resolved_port': str(Path(port).resolve()), 'sysfs': str(p)}
    for ancestor in (p, *p.parents):
        if (ancestor / 'idVendor').exists():
            for key in ('idVendor', 'idProduct', 'manufacturer', 'product', 'serial', 'speed'):
                file = ancestor / key
                result[key] = file.read_text().strip() if file.exists() else None
            break
    return result


class Session:
    def __init__(self, args):
        import serial
        import termios
        self.termios = termios
        self.events = []
        self.started = time.monotonic()
        self.info = describe(args.port)
        if (self.info.get('idVendor'), self.info.get('idProduct')) != ('28e9', '018a'):
            raise RuntimeError('target VID/PID is not the verified probe; refusing I/O')
        self.out = Path(args.out)
        self.out.mkdir(parents=True, exist_ok=True)
        probe = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            self.old_attrs = termios.tcgetattr(probe)
        finally:
            os.close(probe)
        self.info['original_termios'] = [
            [v[0] if isinstance(v, bytes) else v for v in x] if isinstance(x, list) else x
            for x in self.old_attrs]
        # CDC line coding 是控制传输；不等同于下发设备业务配置报文。
        self.ser = serial.Serial(args.port, args.baud, timeout=0.05,
                                 write_timeout=0.5, exclusive=True)
        self.info.update(baud=args.baud, data_bits=8, parity='N', stop_bits=1,
                         dtr=self.ser.dtr, rts=self.ser.rts,
                         started_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                         action=args.action)
        print(json.dumps(self.info, ensure_ascii=False), flush=True)

    def event(self, direction, data, case=None):
        item = {'elapsed_s': round(time.monotonic()-self.started, 6),
                'direction': direction, 'hex': data.hex(' '), 'length': len(data)}
        if case is not None:
            item['case'] = case
        self.events.append(item)
        print(json.dumps(item), flush=True)

    def capture(self, duration, case=None):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            data = self.ser.read(4096)
            if data:
                self.event('RX', data, case)

    def query(self, frame, case, wait):
        if len(frame) >= 4 and frame[:4] == HEAD:
            assert frame[5] == 1 and frame[10] in (2, 6)
        else:
            assert len(frame) == 8 and frame[1] == 3  # Modbus只读保持寄存器。
        count = self.ser.write(frame)
        if count != len(frame):
            raise RuntimeError('partial query write')
        self.ser.flush()
        self.event('TX', frame, case)
        self.capture(wait, case)

    def close(self, error=None):
        try:
            self.termios.tcsetattr(self.ser.fileno(), self.termios.TCSANOW, self.old_attrs)
        except OSError as exc:
            self.info['restore_error'] = str(exc)
        self.ser.close()
        self.info.update(duration_s=round(time.monotonic()-self.started, 3), error=error)
        for direction in ('RX', 'TX'):
            raw = b''.join(bytes.fromhex(e['hex']) for e in self.events if e['direction']==direction)
            (self.out / (direction.lower()+'.bin')).write_bytes(raw)
            self.info[direction.lower()+'_bytes'] = len(raw)
        (self.out/'capture.json').write_text(json.dumps(
            {'metadata': self.info, 'events': self.events}, ensure_ascii=False, indent=2))
        print('SAVED', self.out, 'RX', self.info['rx_bytes'], 'TX', self.info['tx_bytes'], flush=True)


def analyze(file):
    raw = Path(file).read_bytes()
    frames = []
    pos = 0
    while True:
        start = raw.find(HEAD, pos)
        if start < 0:
            break
        end = raw.find(TAIL, start+4)
        if end < 0:
            break
        frame = raw[start:end+4]
        pos = end+4
        item = {'offset': start, 'length': len(frame), 'hex': frame.hex(' ')}
        if len(frame) >= 28:
            item.update(function=frame[5], transaction_hex=frame[6:8].hex(),
                        company=frame[8], product=frame[9], category=frame[10],
                        declared_length_be=int.from_bytes(frame[20:22], 'big'),
                        actual_payload_length=len(frame)-28,
                        payload_hex=frame[22:-6].hex(' '), crc_matches=[])
            for scope, covered in [('payload', frame[22:-6]), ('body', frame[4:-6]), ('head_and_body', frame[:-6])]:
                for name in CRC_SPECS:
                    value = crc16(covered, name)
                    for order in ('little', 'big'):
                        if value.to_bytes(2, order) == frame[-6:-4]:
                            item['crc_matches'].append([name, scope, order])
            if len(frame[22:-6]) >= 12:
                item['serial_ascii'] = frame[22:34].decode('ascii', errors='replace')
        frames.append(item)
    result = {'bytes': len(raw), 'frame_candidates': frames,
              'note': '候选帧不等于已通过CRC；没有帧不能据此推断CRC算法'}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['describe','capture','query','modbus-read','analyze','selftest'])
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--duration', type=float, default=20)
    parser.add_argument('--wait', type=float, default=0.25)
    parser.add_argument('--out', default='/workspace/test/results/session')
    parser.add_argument('--input')
    parser.add_argument('--payload', choices=['ascii_zero','binary_zero','usb_serial','status20'], default='ascii_zero')
    args = parser.parse_args()
    if args.action == 'selftest':
        selftest();return
    if args.action == 'describe':
        print(json.dumps(describe(args.port), ensure_ascii=False, indent=2));return
    if args.action == 'analyze':
        result=analyze(args.input)
        Path(args.input).with_suffix('.analysis.json').write_text(json.dumps(result, ensure_ascii=False, indent=2));return
    if not 0 < args.duration <= 60 or not .05 <= args.wait <= 2:
        parser.error('duration 0..60 and wait 0.05..2 required')
    session = Session(args)
    try:
        session.capture(1, 'before_queries')
        if args.action == 'capture':
            session.capture(args.duration)
        elif args.action == 'query':
            payloads = {'ascii_zero': b'0'*12, 'binary_zero': bytes(12),
                        'usb_serial': b'FFFFFFFEFFFF', 'status20': b'0'*12+bytes(8)}
            transaction=1
            for name in CRC_SPECS:
                for scope in ('body','head_and_body'):
                    for order in ('little','big'):
                        case=f'query_state:{args.payload}:{name}:{scope}:{order}'
                        frame=usb_query(2,payloads[args.payload],transaction,name,scope,order)
                        session.query(frame,case,args.wait)
                        transaction+=1
        elif args.action == 'modbus-read':
            for unit in (1,16):
                for address,count in ((0,81),(6,2)):
                    frame=struct.pack('>BBHH',unit,3,address,count)
                    frame+=crc16(frame,'MODBUS').to_bytes(2,'little')
                    session.query(frame,f'modbus_read:unit={unit}:address={address}:count={count}',.5)
        session.capture(1, 'after_queries')
    except BaseException as exc:
        session.close(str(exc));raise
    else:
        session.close()


if __name__ == '__main__':
    main()
