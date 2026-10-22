#!/usr/bin/env python3
import csv
import json
import tempfile
import unittest
from pathlib import Path

from analyze_video_ack import analyze


class VideoAckTests(unittest.TestCase):
    def fixture(self, directory, ping_sent=1010000000, ping_received=1016000000):
        key = 0xcafebabe12345678
        (directory / 'completed').write_text('complete')
        (directory / 'metadata.json').write_text(json.dumps(dict(
            measurement_start_ns=1000000000, measurement_end_ns=2000000000)))
        with (directory / 'trace_1.csv').open('w', newline='') as stream:
            csv.writer(stream).writerows([
                [999000000, 'encoded', 3600000000010000, 0],
                [1000000000, 'rtp_first', key, 3600000000010000],
                [1001000000, 'rtp_end', key, 3600000000010000],
            ])
        with (directory / 'uart.csv').open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['received_ns', 'event', 'token_or_frame_key', 'processing_us_or_seq', 'sent_ns'])
            writer.writerow([ping_received, 'ping_reply', 7, 1, ping_sent])
            writer.writerow([1020000000, 'video_ack', key, 50, 0])
        return key

    def test_unsigned_frame_key_and_exact_pair(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            result = analyze(directory)
            self.assertEqual(result['samples'], 1)
            self.assertEqual(result['unmatched_ack'], 0)
            self.assertAlmostEqual(result['rtp_last_to_ack_ms']['median'], 19)
            self.assertAlmostEqual(result['estimated_arrival_ms']['median'], 15.95)

    def test_preserve_negative_estimate(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory, 950000000, 1050000000)
            result = analyze(directory)
            self.assertEqual(result['negative_estimates'], 1)
            self.assertLess(result['estimated_arrival_ms']['median'], 0)

    def test_reject_unmatched_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            (directory / 'trace_1.csv').write_text('')
            with self.assertRaisesRegex(RuntimeError, 'No matching'):
                analyze(directory)


if __name__ == '__main__':
    unittest.main()
