import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from analyze_overall import analyze_android


class WindowTests(unittest.TestCase):
    def test_previous_boot_uptime_does_not_pollute_current_fps(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'metadata.json').write_text(json.dumps(dict(start_uptime='0 0', end_uptime='20 0')))
            header = 'wall_time_ms,elapsed_realtime_ms,event,details\n'
            (directory / 'before.csv').write_text(header)
            details = 'buffered_ms=180 release_lead_us=40000 frame_age_ms=10 speed=1 rendered={} dropped=0'
            (directory / 'after.csv').write_text(header +
                '100000,16000,STATS,' + details.format(30) + '\n' +
                '101000,17000,STATS,' + details.format(60) + '\n')
            (directory / 'video_diagnostics.previous.csv').write_text(header +
                '50000,16500,STATS,' + details.format(9000) + '\n')
            with contextlib.redirect_stdout(io.StringIO()):
                result = analyze_android(directory)
            self.assertEqual(result['fresh_stats'], 2)
            self.assertEqual(result['rendered_fps'], 30)

    def test_no_fresh_playback_cannot_be_replaced_by_history(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'metadata.json').write_text(json.dumps(dict(start_uptime='0 0', end_uptime='20 0')))
            old = 'wall_time_ms,elapsed_realtime_ms,event,details\n10000,16000,STATS,buffered_ms=180\n'
            (directory / 'before.csv').write_text(old)
            (directory / 'after.csv').write_text(old)
            with self.assertRaisesRegex(RuntimeError, 'No fresh STATS'):
                analyze_android(directory)


if __name__ == '__main__':
    unittest.main()
