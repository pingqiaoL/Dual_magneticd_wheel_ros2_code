"""Test-only RTSP request normalization; H.264/RTP passes through unchanged."""
import select
import socket
import threading

def serve(client):
    upstream = socket.create_connection(('127.0.0.1', 18555), timeout=5)
    upstream.settimeout(None)
    pending = b''
    try:
        while True:
            readable, _, _ = select.select([client, upstream], [], [], 30)
            if not readable:
                continue
            for source in readable:
                data = source.recv(65536)
                if not data:
                    return
                if source is upstream:
                    client.sendall(data)
                    continue
                pending += data
                while pending:
                    if pending[:1] == b'$':
                        if len(pending) < 4:
                            break
                        size = 4 + int.from_bytes(pending[2:4], 'big')
                        if len(pending) < size:
                            break
                        upstream.sendall(pending[:size])
                        pending = pending[size:]
                        continue
                    end = pending.find(b'\r\n\r\n')
                    if end < 0:
                        break
                    header = pending[:end + 4]
                    lines = header.split(b'\r\n')
                    length = next((int(line.split(b':', 1)[1]) for line in lines
                                   if line.lower().startswith(b'content-length:')), 0)
                    total = end + 4 + length
                    if len(pending) < total:
                        break
                    fixed = []
                    for line in lines:
                        if line.lower().startswith(b'range:') and line.split(b':', 1)[1].strip() == b'0-':
                            print('REMOVED_INVALID_RANGE', lines[0].decode(), flush=True)
                            continue
                        fixed.append(line)
                    upstream.sendall(b'\r\n'.join(fixed) + pending[end+4:total])
                    pending = pending[total:]
    except (OSError, ValueError) as error:
        print('CONNECTION_END', str(error), flush=True)
    finally:
        client.close()
        upstream.close()

server = socket.socket()
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('0.0.0.0', 18554))
server.listen(4)
print('TEST_PROXY_LISTEN 18554 -> 8554', flush=True)
while True:
    client, address = server.accept()
    print('CLIENT', address, flush=True)
    threading.Thread(target=serve, args=(client,), daemon=True).start()

