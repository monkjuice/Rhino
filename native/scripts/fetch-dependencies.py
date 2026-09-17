"""Fetch the exact engine and JUCE revisions used by the native evaluation."""
import io
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1] / '.deps'
DEPENDENCIES = (
    ('tracktion', 'Tracktion/tracktion_engine', '4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'),
    ('juce', 'juce-framework/JUCE', '37c894f83d379179b2070d437ccd0f1cd9af9576'),
)

for name, repository, revision in DEPENDENCIES:
    destination = ROOT / name
    marker = destination / '.rhino-revision'
    legacy_marker = destination / '.theda-revision'
    if legacy_marker.exists() and not marker.exists():
        legacy_marker.rename(marker)
    if marker.exists() and marker.read_text() == revision:
        print(f'{name}: already at {revision}', flush=True)
        continue
    if destination.exists():
        raise SystemExit(f'{destination} exists without the expected revision marker')
    print(f'Fetching {repository} at {revision}', flush=True)
    request = urllib.request.Request(
        f'https://api.github.com/repos/{repository}/tarball/{revision}',
        headers={'User-Agent': 'Rhino-native-build'},
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        archive = response.read()
    ROOT.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as tar:
        prefix = tar.getmembers()[0].name.split('/')[0]
        tar.extractall(ROOT, filter='data')
    (ROOT / prefix).rename(destination)
    marker.write_text(revision)
    print(f'{name}: ready', flush=True)
