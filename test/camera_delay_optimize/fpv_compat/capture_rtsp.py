import socket,struct,time
s=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(0x0003))
s.settimeout(1)
end=time.monotonic()+18
while time.monotonic()<end:
 try: b=s.recv(65535)
 except socket.timeout: continue
 if len(b)<54 or b[12:14]!=b'\x08\x00': continue
 ip=b[14:]; ih=(ip[0]&15)*4
 if ip[9]!=6: continue
 tcp=ip[ih:]; sp,dp=struct.unpack('!HH',tcp[:4])
 if 8554 not in (sp,dp): continue
 off=(tcp[12]>>4)*4
 payload=tcp[off:]
 if payload:
  print(socket.inet_ntoa(ip[12:16]),sp,'->',socket.inet_ntoa(ip[16:20]),dp,repr(payload[:5000]),flush=True)
