#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

root = Path('/workspace')
suite = Path(__file__).resolve().parent
selected = json.loads((suite / 'results/selected_nano.json').read_text())
for relative, digest in selected['source_hashes'].items():
    assert hashlib.sha256((root / 'src/video_streamer_transmission' / relative).read_bytes()).hexdigest() == digest
components = []
for process in Path('/proc').iterdir():
    if not process.name.isdigit():
        continue
    try:
        command = (process / 'cmdline').read_bytes().split(b'\0')
        if b'__node:=video_container' in command:
            maps = (process / 'maps').read_text()
            components.append(dict(pid=int(process.name),
                formal_library=str((root / 'install/video_streamer_transmission/lib/libvideo_component.so').resolve()) in maps,
                candidate_library='optimization_workspaces' in maps))
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
assert len(components) == 1 and components[0]['formal_library'] and not components[0]['candidate_library'], components
result = dict(selected_sources_match=True, components=components, source_files=list(selected['source_hashes']))
(suite / 'results/final_runtime_verification.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
