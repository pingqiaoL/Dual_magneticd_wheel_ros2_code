#!/usr/bin/env python3
"""Join RTP keys with UART video ACKs; report closure and explicitly approximate arrival."""

import argparse
import csv
import json
from pathlib import Path

from analyze import distribution


def analyze(directory):
    metadata = json.loads((directory / 'metadata.json').read_text())
    if not (directory / 'completed').exists() or metadata['measurement_start_ns'] is None:
        raise RuntimeError('No complete ACK collection')
    start, end = metadata['measurement_start_ns'], metadata['measurement_end_ns']
    rtp_first, rtp_end, encoded = {}, {}, {}
    for path in directory.glob('trace_*.csv'):
        with path.open() as stream:
            for timestamp, stage, frame_id, related_id in csv.reader(stream):
                timestamp, frame_id, related_id = int(timestamp), int(frame_id), int(related_id)
                if stage == 'rtp_first':
                    rtp_first[frame_id] = timestamp
                elif stage == 'rtp_end':
                    rtp_end[frame_id] = (timestamp, related_id)
                elif stage == 'encoded':
                    encoded[frame_id] = timestamp
    with (directory / 'uart.csv').open() as stream:
        uart = list(csv.DictReader(stream))
    pings = [(int(row['received_ns']), int(row['sent_ns'])) for row in uart if row['event'] == 'ping_reply']
    rows, seen = [], set()
    unmatched = 0
    for row in uart:
        received = int(row['received_ns'])
        if row['event'] != 'video_ack' or not start <= received < end:
            continue
        key = int(row['token_or_frame_key'])
        if key in seen:
            continue
        seen.add(key)
        if key not in rtp_end or key not in rtp_first:
            unmatched += 1
            continue
        last_packet, pts = rtp_end[key]
        first_packet = rtp_first[key]
        encoded_at = encoded.get(pts)
        processing = int(row['processing_us_or_seq']) / 1000
        # Use nearby RTTs measured by the same Android process and same 26-byte response size.
        nearby = [(receive_ns - sent_ns) / 1e6 for receive_ns, sent_ns in pings
                  if abs((receive_ns + sent_ns) // 2 - received) <= 3000000000]
        from statistics import median
        local_rtt = median(nearby) if nearby else None
        closure = (received - last_packet) / 1e6
        estimate = closure - processing - local_rtt / 2 if local_rtt is not None else None
        if closure < 0:
            raise RuntimeError('ACK predates matching frame; reject frame association')
        rows.append([key, first_packet, last_packet, encoded_at, received, processing,
                     closure, (received - first_packet) / 1e6,
                     (received - encoded_at) / 1e6 if encoded_at else None, local_rtt, estimate])
    with (directory / 'video_ack_frames.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['frame_key', 'rtp_first_ns', 'rtp_last_ns', 'encoded_ns', 'ack_received_ns',
                         'android_processing_ms', 'last_rtp_to_ack_ms', 'first_rtp_to_ack_ms',
                         'encoded_to_ack_ms', 'nearby_ping_rtt_ms', 'estimated_arrival_ms'])
        writer.writerows(rows)
    if not rows:
        raise RuntimeError('No matching RTP/ACK samples; cannot report a video delay')
    measured_pings = [(receive_ns - sent_ns) / 1e6 for receive_ns, sent_ns in pings if start <= receive_ns < end]
    result = dict(samples=len(rows), unmatched_ack=unmatched,
                  same_process_ping_rtt_ms=distribution(measured_pings),
                  rtp_last_to_ack_ms=distribution([row[6] for row in rows]),
                  rtp_first_to_ack_ms=distribution([row[7] for row in rows]),
                  encoded_to_ack_ms=distribution([row[8] for row in rows if row[8] is not None]),
                  android_processing_ms=distribution([row[5] for row in rows]),
                  estimated_arrival_ms=distribution([row[10] for row in rows if row[10] is not None]),
                  negative_estimates=sum(row[10] is not None and row[10] < 0 for row in rows),
                  boundary='RTP payloader pad, not physical NIC egress; complete-frame test receiver, no decode/render',
                  assumptions='Estimated arrival subtracts nearby RTT/2; assumes data-link symmetry and similar processing. Not exact one-way delay.')
    (directory / 'video_ack_summary.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    analyze(parser.parse_args().directory)
