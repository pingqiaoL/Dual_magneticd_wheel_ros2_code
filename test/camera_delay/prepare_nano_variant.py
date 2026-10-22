#!/usr/bin/env python3
import json
import shutil
import hashlib
from pathlib import Path

suite = Path(__file__).resolve().parent
root = Path('/workspace')
runtime = suite / 'optimization_workspaces/nano_latest'
destination = runtime / 'src/video_streamer_transmission'
shutil.copytree(root / 'src/video_streamer_transmission', destination)
paths = ['module/VideoPipeline.cpp', 'include/video_streamer_transmission/VideoTypes.hpp', 'config/video.yaml']
manifest = {path: hashlib.sha256((root / 'src/video_streamer_transmission' / path).read_bytes()).hexdigest() for path in paths}
(runtime / 'baseline_manifest.json').write_text(json.dumps(manifest, indent=2))
for relative in paths:
    path = destination / relative
    text = path.read_text()
    replacements = {'module/VideoPipeline.cpp': [('max-buffers=2', 'max-buffers=1'), ('max-size-buffers=2', 'max-size-buffers=1')],
                    'include/video_streamer_transmission/VideoTypes.hpp': [('max_frame_age_ms = 500', 'max_frame_age_ms = 150')],
                    'config/video.yaml': [('max_frame_age_ms: 500', 'max_frame_age_ms: 150')]}
    for old, new in replacements[relative]:
        assert text.count(old) == 1, relative
        text = text.replace(old, new)
    path.write_text(text)
print(runtime)
