import java.io.*;
import java.net.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

/** Test-only console program, run by app_process; no APK installation. */
public final class VideoArrivalProbe {
    private final Object serialLock = new Object();
    private FileOutputStream serialOutput;
    private FileInputStream serialInput;
    private volatile boolean running = true;
    private int mavlinkSequence;
    private volatile long serialBytes, serialPings, serialCrcErrors, serialEmptyReads;
    private final ByteArrayOutputStream serialSample = new ByteArrayOutputStream();

    private static int crcAccumulate(int crc, int value) {
        int temporary = (value ^ crc) & 255;
        temporary ^= (temporary << 4) & 255;
        return ((crc >>> 8) ^ (temporary << 8) ^ (temporary << 3) ^ (temporary >>> 4)) & 65535;
    }

    private static byte[] pingPacket(int sequence, long token, long pingSequence, int system, int component) {
        byte[] packet = new byte[26];
        packet[0] = (byte) 253; packet[1] = 14; packet[4] = (byte) sequence;
        packet[5] = (byte) 255; packet[6] = (byte) 190; packet[7] = 4;
        ByteBuffer payload = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN);
        payload.position(10);
        payload.putLong(token).putInt((int) pingSequence).put((byte) system).put((byte) component);
        int crc = 65535;
        for (int index = 1; index < 24; index++) crc = crcAccumulate(crc, packet[index] & 255);
        crc = crcAccumulate(crc, 237);
        packet[24] = (byte) crc; packet[25] = (byte) (crc >>> 8);
        return packet;
    }

    private void replyPing(long token, long sequence, int system, int component) throws IOException {
        synchronized (serialLock) {
            serialOutput.write(pingPacket(mavlinkSequence++ & 255, token, sequence, system, component));
        }
    }

    private void frameAcknowledgement(long key, long receivedNs) throws IOException {
        synchronized (serialLock) {
            long processingUs = Math.max(0, (System.nanoTime() - receivedNs) / 1000);
            if (processingUs >= 0x80000000L) throw new IOException("ACK processing overflow");
            // Test-only PING carrier: high seq bit marks video ACK; remaining bits are local processing us.
            serialOutput.write(pingPacket(mavlinkSequence++ & 255, key, 0x80000000L | processingUs, 1, 191));
            System.out.println("ACK," + Long.toUnsignedString(key) + "," + receivedNs + "," + processingUs);
        }
    }

    private void readSerial() {
        try {
            byte[] pending = new byte[8192];
            byte[] input = new byte[512];
            int used = 0;
            while (running) {
                int count = serialInput.read(input);
                if (count <= 0) { serialEmptyReads++; Thread.sleep(10); continue; }
                serialBytes += count;
                if (serialSample.size() < 512) serialSample.write(input, 0, Math.min(count, 512 - serialSample.size()));
                if (used + count > pending.length) used = 0;
                System.arraycopy(input, 0, pending, used, count); used += count;
                int position = 0;
                while (position + 2 <= used) {
                int magic = pending[position] & 255;
                if (magic != 253 && magic != 254) { position++; continue; }
                int length = pending[position + 1] & 255;
                int headerLength = magic == 253 ? 10 : 6;
                int packetLength = headerLength + length + 2;
                if (position + packetLength > used) break;
                byte[] packet = Arrays.copyOfRange(pending, position, position + packetLength);
                position += packetLength;
                if (magic == 253 && packet[2] != 0) continue;
                int messageId = magic == 253 ? ((packet[7] & 255) | ((packet[8] & 255) << 8) | ((packet[9] & 255) << 16)) : packet[5] & 255;
                if (messageId != 4) continue;
                int crc = 65535;
                for (int index = 1; index < packet.length - 2; index++) crc = crcAccumulate(crc, packet[index] & 255);
                crc = crcAccumulate(crc, 237);
                if (crc != ((packet[packet.length - 2] & 255) | ((packet[packet.length - 1] & 255) << 8))) { serialCrcErrors++; continue; }
                serialPings++;
                byte[] payload = new byte[14];
                System.arraycopy(packet, headerLength, payload, 0, Math.min(length, 14));
                ByteBuffer fields = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN);
                long token = fields.getLong(); long sequence = Integer.toUnsignedLong(fields.getInt());
                int system = fields.get() & 255; int component = fields.get() & 255;
                if (system == 0 && component == 0) {
                    replyPing(token, sequence, packet[magic == 253 ? 5 : 3] & 255,
                              packet[magic == 253 ? 6 : 4] & 255);
                }
                }
                if (position > 0) {
                    System.arraycopy(pending, position, pending, 0, used - position);
                    used -= position;
                }
            }
        } catch (IOException | InterruptedException error) {
            if (running) System.err.println("SERIAL_ERROR " + error);
        }
    }

    private static void readFully(InputStream input, byte[] bytes, int offset, int length) throws IOException {
        while (length > 0) {
            int count = input.read(bytes, offset, length);
            if (count < 0) throw new EOFException();
            offset += count; length -= count;
        }
    }

    private static final class Response {
        String status;
        Map<String, String> headers = new HashMap<>();
        String body;
    }

    private static String line(InputStream input) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        int value;
        while ((value = input.read()) >= 0) {
            if (value == '\n') return new String(bytes.toByteArray(), StandardCharsets.US_ASCII).trim();
            bytes.write(value);
            if (bytes.size() > 16384) throw new IOException("RTSP header too long");
        }
        throw new EOFException();
    }

    private static Response request(Socket socket, int sequence, String method, String uri, String headers) throws IOException {
        String text = method + " " + uri + " RTSP/1.0\r\nCSeq: " + sequence + "\r\nUser-Agent: CameraDelayTest\r\n" + headers + "\r\n";
        socket.getOutputStream().write(text.getBytes(StandardCharsets.US_ASCII));
        InputStream input = socket.getInputStream();
        Response response = new Response();
        response.status = line(input);
        String header;
        while (!(header = line(input)).isEmpty()) {
            int separator = header.indexOf(':');
            if (separator > 0) response.headers.put(header.substring(0, separator).toLowerCase(Locale.ROOT), header.substring(separator + 1).trim());
        }
        int length = Integer.parseInt(response.headers.getOrDefault("content-length", "0"));
        if (length < 0 || length > 65536) throw new IOException("Invalid RTSP body length");
        byte[] body = new byte[length]; readFully(input, body, 0, length);
        response.body = new String(body, StandardCharsets.US_ASCII);
        System.out.println("RTSP," + method + "," + response.status);
        if (!response.status.contains(" 200 ")) throw new IOException(response.status);
        return response;
    }

    private static long unsigned32(byte[] bytes, int offset) {
        return ((long) (bytes[offset] & 255) << 24) | ((long) (bytes[offset + 1] & 255) << 16)
                | ((long) (bytes[offset + 2] & 255) << 8) | (bytes[offset + 3] & 255);
    }

    private static boolean startsVcl(byte[] bytes, int offset, int length) {
        if (offset >= length) return false;
        int type = bytes[offset] & 31;
        if (type == 1 || type == 5) return true;
        if (type == 28 && offset + 1 < length) {
            int fragmentType = bytes[offset + 1] & 31;
            return (bytes[offset + 1] & 128) != 0 && (fragmentType == 1 || fragmentType == 5);
        }
        if (type == 24) {
            int cursor = offset + 1;
            while (cursor + 2 < length) {
                int size = ((bytes[cursor] & 255) << 8) | (bytes[cursor + 1] & 255);
                cursor += 2;
                if (size < 1 || cursor + size > length) return false;
                int nestedType = bytes[cursor] & 31;
                if (nestedType == 1 || nestedType == 5) return true;
                cursor += size;
            }
        }
        return false;
    }

    private void run(String host, String interfaceName, String serialPath, int durationSeconds) throws Exception {
        NetworkInterface network = NetworkInterface.getByName(interfaceName);
        if (network == null || !network.isUp()) throw new IOException("Required interface absent/down: " + interfaceName);
        InetAddress local = null;
        Enumeration<InetAddress> addresses = network.getInetAddresses();
        while (addresses.hasMoreElements()) {
            InetAddress address = addresses.nextElement();
            if (address instanceof Inet4Address) local = address;
        }
        if (local == null) throw new IOException("Required interface has no IPv4: " + interfaceName);
        System.out.println("NETWORK," + interfaceName + "," + local.getHostAddress());
        Process configure = new ProcessBuilder("/system/bin/stty", "-F", serialPath,
                "115200", "raw", "cs8", "min", "1", "time", "0").redirectErrorStream(true).start();
        if (configure.waitFor() != 0) throw new IOException("stty failed");
        // This controller's stty mishandles individual negative input flags. Use its
        // saved-format setter, preserving control characters and speed, then verify.
        Process getSettings = new ProcessBuilder("/system/bin/stty", "-F", serialPath, "-g").start();
        String settings = new BufferedReader(new InputStreamReader(getSettings.getInputStream())).readLine();
        if (getSettings.waitFor() != 0 || settings == null) throw new IOException("stty -g failed");
        String[] fields = settings.trim().split(":");
        if (fields.length < 5) throw new IOException("Unexpected stty saved format");
        fields[0] = "0"; fields[1] = "0"; fields[3] = "0";
        long controlFlags = Long.parseLong(fields[2], 16);
        controlFlags = (controlFlags & ~(0x100L | 0x40L | 0x80000000L | 0x30L)) | 0x30L | 0x80L | 0x800L;
        fields[2] = Long.toHexString(controlFlags);
        Process setSettings = new ProcessBuilder("/system/bin/stty", "-F", serialPath, String.join(":", fields)).start();
        if (setSettings.waitFor() != 0) throw new IOException("stty saved settings failed");
        Process verifySettings = new ProcessBuilder("/system/bin/stty", "-F", serialPath, "-g").start();
        String verified = new BufferedReader(new InputStreamReader(verifySettings.getInputStream())).readLine();
        if (verifySettings.waitFor() != 0 || !String.join(":", fields).equals(verified))
            throw new IOException("Serial settings verification failed: " + verified);
        System.out.println("SERIAL_CONFIG," + verified);
        serialInput = new FileInputStream(serialPath);
        serialOutput = new FileOutputStream(serialPath);
        Thread serialReader = new Thread(this::readSerial, "delay-uart");
        serialReader.setDaemon(true); serialReader.start();

        try (DatagramSocket rtp = new DatagramSocket(new InetSocketAddress(local, 56000));
             DatagramSocket rtcp = new DatagramSocket(new InetSocketAddress(local, 56001));
             Socket control = new Socket()) {
            rtp.setReceiveBufferSize(1024 * 1024);
            rtp.setSoTimeout(1000);
            control.bind(new InetSocketAddress(local, 0));
            control.connect(new InetSocketAddress(host, 8554), 5000);
            control.setSoTimeout(5000);
            String uri = "rtsp://" + host + ":8554/live";
            Response description = request(control, 1, "DESCRIBE", uri, "Accept: application/sdp\r\n");
            String base = description.headers.getOrDefault("content-base", uri + "/");
            String track = null;
            boolean video = false;
            for (String entry : description.body.split("\\r?\\n")) {
                if (entry.startsWith("m=")) video = entry.startsWith("m=video");
                if (video && entry.startsWith("a=control:")) track = entry.substring(10);
            }
            if (track == null) throw new IOException("No video control track in SDP");
            String trackUri = track.startsWith("rtsp://") ? track : new URI(base).resolve(track).toString();
            Response setup = request(control, 2, "SETUP", trackUri,
                    "Transport: RTP/AVP;unicast;client_port=56000-56001\r\n");
            String session = setup.headers.get("session");
            if (session == null) throw new IOException("No RTSP session");
            session = session.split(";")[0];
            request(control, 3, "PLAY", uri, "Session: " + session + "\r\n");
            long deadline = System.nanoTime() + durationSeconds * 1000000000L;
            long lastAckNs = 0, currentKey = Long.MIN_VALUE;
            int previousSequence = -1;
            boolean contiguous = true, vclStart = false;
            long packets = 0, complete = 0, damaged = 0, acknowledgements = 0;
            ScheduledExecutorService keepalive = Executors.newSingleThreadScheduledExecutor();
            final String sessionId = session;
            final int[] controlSequence = {4};
            keepalive.scheduleAtFixedRate(() -> {
                try {
                    synchronized (control) {
                        request(control, controlSequence[0]++, "OPTIONS", uri, "Session: " + sessionId + "\r\n");
                    }
                } catch (IOException error) {
                    System.err.println("KEEPALIVE_ERROR " + error);
                }
            }, 15, 15, TimeUnit.SECONDS);
            byte[] buffer = new byte[65535];
            DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
            while (System.nanoTime() < deadline) {
                packet.setLength(buffer.length);
                try { rtp.receive(packet); } catch (SocketTimeoutException timeout) { continue; }
                long arrivedNs = System.nanoTime();
                int length = packet.getLength();
                if (length < 12 || (buffer[0] & 192) != 128) continue;
                long key = (unsigned32(buffer, 8) << 32) | unsigned32(buffer, 4);
                int sequence = ((buffer[2] & 255) << 8) | (buffer[3] & 255);
                int offset = 12 + 4 * (buffer[0] & 15);
                if ((buffer[0] & 16) != 0) {
                    if (offset + 4 > length) continue;
                    int words = ((buffer[offset + 2] & 255) << 8) | (buffer[offset + 3] & 255);
                    offset += 4 + 4 * words;
                }
                if (offset >= length) continue;
                packets++;
                if (key != currentKey) {
                    currentKey = key; contiguous = true; vclStart = false;
                }
                if (previousSequence != -1 && sequence != ((previousSequence + 1) & 65535)) contiguous = false;
                previousSequence = sequence;
                vclStart |= startsVcl(buffer, offset, length);
                if ((buffer[1] & 128) != 0) {
                    if (contiguous && vclStart) {
                        complete++;
                        if (arrivedNs - lastAckNs >= 1000000000L) {
                            frameAcknowledgement(key, arrivedNs);
                            lastAckNs = arrivedNs; acknowledgements++;
                        }
                    } else { damaged++; }
                }
            }
            keepalive.shutdownNow();
            synchronized (control) {
                request(control, controlSequence[0], "TEARDOWN", uri, "Session: " + session + "\r\n");
            }
            System.out.println("SUMMARY,packets=" + packets + ",complete=" + complete
                    + ",damaged=" + damaged + ",ack=" + acknowledgements
                    + ",serial_bytes=" + serialBytes + ",serial_pings=" + serialPings
                    + ",serial_crc_errors=" + serialCrcErrors + ",serial_empty_reads=" + serialEmptyReads);
            StringBuilder serialHex = new StringBuilder();
            for (byte value : serialSample.toByteArray()) serialHex.append(String.format("%02x", value & 255));
            System.out.println("SERIAL_SAMPLE," + serialHex);
        } finally {
            running = false;
            serialInput.close(); serialOutput.close();
        }
    }

    public static void main(String[] args) {
        try {
            if (args.length == 1 && args[0].equals("selftest")) {
                if (!startsVcl(new byte[]{0x7c, (byte) 0x85}, 0, 2)
                        || startsVcl(new byte[]{0x7c, 0x05}, 0, 2)
                        || startsVcl(new byte[]{0x7c, (byte) 0x87}, 0, 2))
                    throw new IllegalStateException("H264 FU-A start validation failed");
                byte[] packet = pingPacket(7, 0xcafebabe12345678L, 0x8000007bL, 1, 191);
                StringBuilder hex = new StringBuilder();
                for (byte value : packet) hex.append(String.format("%02x", value & 255));
                System.out.println(hex);
                return;
            }
            if (args.length != 4) throw new IllegalArgumentException("host interface UART durationSeconds");
            new VideoArrivalProbe().run(args[0], args[1], args[2], Integer.parseInt(args[3]));
            System.exit(0);
        } catch (Exception error) {
            error.printStackTrace(); System.exit(1);
        }
    }
}
