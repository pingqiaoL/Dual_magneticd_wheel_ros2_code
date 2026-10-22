#!/usr/bin/env python3
"""Validate frame matching and monotonic timing without a camera."""

import csv
import json
import tempfile
import unittest
from pathlib import Path

from analyze import analyze


class AnalysisTests(unittest.TestCase):
    def fixture(self, directory):
        (directory / 'metadata.json').write_text(json.dumps(dict(
            measurement_start_ns=1000, measurement_end_ns=1000001000, seconds=1)))
        rows = [(100, 'source', 5, 0), (200, 'sample', 5, 0),
                (300, 'publish', 900000000000000000, 5),  # warmup: exclude
                (1100, 'source', 6, 0), (1200, 'sample', 6, 0),
                (1250, 'copy_complete', 6, 0), (1300, 'publish', 900000000000000001, 6),
                (1400, 'video_receive', 900000000000000001, 0),
                (1500, 'push', 900000000000000001, 1234),
                (1550, 'encoder_input', 1234, 0), (1700, 'encoded', 3600000000001234, 0)]
        with (directory / 'trace_1.csv').open('w') as stream:
            csv.writer(stream).writerows(rows)
        (directory / 'viewer.csv').write_text('1450,receive,900000000000000001,0\n')
        (directory / 'resources.csv').write_text('monotonic_ns,cpu_total_ticks,cpu_idle_ticks\n1000,100,50\n2000,200,100\n')

    def test_exact_frame_join_and_warmup(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            result = analyze(directory)
            self.assertEqual(result['metrics']['capture_pipeline_ms']['count'], 1)
            self.assertAlmostEqual(result['metrics']['ros_to_viewer_ms']['median'], 0.00015)
            self.assertAlmostEqual(result['metrics']['encode_and_parse_ms']['median'], 0.00015)
            self.assertEqual(result['encoding_match']['match_ratio'], 1)

    def test_reject_wrong_encoded_pairing(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            with (directory / 'trace_1.csv').open('a') as stream:
                stream.write('1800,encoded,3600000000009999,0\n')
            with self.assertRaisesRegex(RuntimeError, 'reliably pair'):
                analyze(directory)

    def test_reject_negative_duration(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            with (directory / 'trace_1.csv').open('a') as stream:
                stream.write('1050,sample,6,0\n')
            with self.assertRaisesRegex(RuntimeError, 'Negative'):
                analyze(directory)


if __name__ == '__main__':
    unittest.main()
