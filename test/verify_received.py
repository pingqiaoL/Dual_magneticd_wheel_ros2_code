#!/usr/bin/env python3
"""按实测长度和载荷CRC检验收到的帧，保留材料原码与两种浮点解释。"""
import argparse,json,math,struct
from pathlib import Path
from usb_protocol_lab import HEAD,TAIL,crc16
p=argparse.ArgumentParser(description=__doc__);p.add_argument('input');a=p.parse_args()
raw=Path(a.input).read_bytes();pos=0;frames=[];bad=[]
while True:
 start=raw.find(HEAD,pos)
 if start<0:break
 if start+22>len(raw):bad.append({'offset':start,'error':'incomplete header'});break
 length=int.from_bytes(raw[start+20:start+22],'big')
 if length>4096:bad.append({'offset':start,'error':'length too large'});pos=start+1;continue
 end=start+28+length
 if end>len(raw):bad.append({'offset':start,'error':'incomplete frame'});break
 frame=raw[start:end];payload=frame[22:-6]
 if frame[-4:]!=TAIL or crc16(payload,'MODBUS').to_bytes(2,'big')!=frame[-6:-4]:
  bad.append({'offset':start,'error':'tail or payload CRC mismatch'});pos=start+1;continue
 item={'offset':start,'length':len(frame),'function':frame[5],'category':frame[10],'payload_length':length,'serial':payload[:12].decode('ascii',errors='replace'),'payload_hex':payload.hex(' '),'crc_hex':frame[-6:-4].hex(' '),'hex':frame.hex(' ')}
 if frame[10]==5 and length==17:
  item.update(material_raw=payload[12],value_be=struct.unpack('>f',payload[13:17])[0],value_le=struct.unpack('<f',payload[13:17])[0])
 if frame[10]==2 and length==20:
  item.update(current_probe=payload[12],calibration_probe=payload[13],menu_mode=payload[14],device_probe=payload[15],calibration_thickness_be=struct.unpack('>f',payload[16:20])[0])
 frames.append(item);pos=end
measurement=[x for x in frames if x['category']==5 and x['payload_length']==17]
result={'input':a.input,'bytes':len(raw),'valid_frames':len(frames),'bad_candidates':bad,'category_counts':{str(c):sum(x['category']==c for x in frames) for c in sorted(set(x['category'] for x in frames))},'measurement_count':len(measurement),'first_measurements':measurement[:8],'frames':frames}
if measurement:
 vals=[x['value_be'] for x in measurement if math.isfinite(x['value_be'])]
 if vals:result['measurement_be_range']=[min(vals),max(vals)]
out=Path(a.input).with_suffix('.verified.json');out.write_text(json.dumps(result,ensure_ascii=False,indent=2))
print(json.dumps({k:v for k,v in result.items() if k not in ('frames','first_measurements')},ensure_ascii=False,indent=2));print('SAVED',out)
