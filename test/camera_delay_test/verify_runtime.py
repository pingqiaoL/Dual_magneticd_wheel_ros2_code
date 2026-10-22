#!/usr/bin/env python3
"""Check production source hashes and that restored components use production libs."""

import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--expected-viewers', type=int, default=1)
    arguments = parser.parse_args()
    root = Path('/workspace')
    manifest = json.loads((arguments.runtime_root / 'source_manifest.json').read_text())
    changed = [relative for relative, checksum in manifest.items()
               if hashlib.sha256((root / relative).read_bytes()).hexdigest() != checksum]
    components = []
    viewers = []
    test_runners = []
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            command = (process / 'cmdline').read_bytes().split(b'\0')
            if b'__node:=video_container' in command:
                maps = (process / 'maps').read_text()
                components.append(dict(pid=int(process.name),
                    production_camera=str(Path('/workspace/install/camera_driver/lib/libcamera_component.so').resolve()) in maps,
                    production_video=str(Path('/workspace/install/video_streamer_transmission/lib/libvideo_component.so').resolve()) in maps,
                    instrumented_libraries='/workspace/log/camera_delay_build_' in maps))
            if b'/workspace/tools/view_raw_image.py' in command:
                viewers.append(int(process.name))
            if b'/workspace/test/camera_delay_test/run_matrix.py' in command:
                test_runners.append(int(process.name))
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    result = dict(changed_formal_files=changed, components=components,
                  viewers=viewers, remaining_test_runners=test_runners)
    print(json.dumps(result, indent=2))
    assert not changed and not test_runners, result
    assert len(components) == 1 and len(viewers) == arguments.expected_viewers, result
    assert components[0]['production_camera'] and components[0]['production_video'], result
    assert not components[0]['instrumented_libraries'], result


if __name__ == '__main__':
    main()
