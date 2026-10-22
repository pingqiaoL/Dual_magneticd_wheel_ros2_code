#!/usr/bin/env python3
"""Apply the measured 100/180/200 policy, refusing to overwrite concurrent edits."""
import hashlib
import json
import shutil
from pathlib import Path

suite = Path(__file__).resolve().parent
project = suite.parents[1]
variant = suite / 'optimization_workspaces/android_100'
manifest = json.loads((variant / 'baseline_manifest.json').read_text())
for relative, digest in manifest.items():
    if hashlib.sha256((project / relative).read_bytes()).hexdigest() != digest:
        raise RuntimeError('Concurrent source change: ' + relative)
for relative in manifest:
    if relative != 'app/build.gradle':
        shutil.copy2(variant / relative, project / relative)
build = project / 'app/build.gradle'
text = build.read_text()
assert text.count('versionCode 24') == 1 and text.count("versionName '0.8.9'") == 1
build.write_text(text.replace('versionCode 24', 'versionCode 25').replace("versionName '0.8.9'", "versionName '0.8.10'"))
(suite / 'results/selected_android.json').write_text(json.dumps(dict(
    target_ms=100, trigger_ms=180, hold_ms=200, version='0.8.10', version_code=25,
    comparison='results/android_20261020_205623',
    original_apk='results/optimization_backup/original_0.8.9.apk'), indent=2))
print('Applied 0.8.10, policy 100/180/200')
