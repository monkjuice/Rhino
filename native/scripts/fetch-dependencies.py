"""Fetch the exact engine and JUCE revisions used by the native evaluation."""
import io
from pathlib import Path
import tarfile
import time
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
    # Windows can still hold the freshly written tree open when extraction
    # returns -- a virus scanner following the writes is enough -- and the
    # rename then fails with "Access is denied" on a directory that is
    # complete and correct. Retry rather than abort: the caller is left with
    # an unnamed tree the script would otherwise refuse to adopt on rerun.
    for attempt in range(20):
        try:
            (ROOT / prefix).rename(destination)
            break
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.5)
    marker.write_text(revision)
    print(f'{name}: ready', flush=True)
