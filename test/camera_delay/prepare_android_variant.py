#!/usr/bin/env python3
"""Build a separate project copy for catch-up policy A/B testing."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--target', type=int, choices=(80, 100, 120), required=True)
parser.add_argument('--trigger', type=int)
args = parser.parse_args()
trigger = args.trigger or (180 if args.target == 100 else 150)
if trigger <= args.target:
    parser.error('Trigger must exceed target')
suite = Path(__file__).resolve().parent
project = suite.parents[1]
label = str(args.target) + ('_' + str(trigger) if args.trigger else '')
output = suite / 'optimization_workspaces' / ('android_' + label)
output.mkdir(parents=True, exist_ok=False)
for name in ('build.gradle', 'settings.gradle', 'gradle.properties', 'local.properties', 'gradlew', 'gradlew.bat'):
    shutil.copy2(project / name, output / name)
shutil.copytree(project / 'gradle', output / 'gradle')
shutil.copytree(project / 'app/src', output / 'app/src')
shutil.copy2(project / 'app/build.gradle', output / 'app/build.gradle')
shutil.copy2(project / 'app/proguard-rules.pro', output / 'app/proguard-rules.pro')
relative = Path('app/src/main/java/com/climbwheel/mk32/video/LivePlaybackCatchUp.java')
text = (output / relative).read_text()
for old, new in [('TARGET_BUFFER_MS = 180', 'TARGET_BUFFER_MS = ' + str(args.target)),
                 ('START_BUFFER_MS = 300', 'START_BUFFER_MS = ' + str(trigger)),
                 ('START_HOLD_MS = 300', 'START_HOLD_MS = 200')]:
    assert text.count(old) == 1
    text = text.replace(old, new)
(output / relative).write_text(text)
test_relative = Path('app/src/test/java/com/climbwheel/mk32/video/LivePlaybackCatchUpTest.java')
shutil.copy2(suite / 'LivePlaybackCatchUpTest.java', output / test_relative)
build = output / 'app/build.gradle'
build.write_text(build.read_text().replace("versionName '0.8.9'", "versionName '0.8.9-test" + label.replace('_', '-') + "'"))
manifest = {str(path): hashlib.sha256((project / path).read_bytes()).hexdigest()
            for path in (relative, test_relative, Path('app/build.gradle'))}
(output / 'baseline_manifest.json').write_text(json.dumps(manifest, indent=2))
print(output)
