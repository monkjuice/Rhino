"""Fetch read-only source snapshots for architecture research; never execute them."""
import concurrent.futures
import json
import pathlib
import urllib.request
import sys
import hashlib
import base64

OUT = pathlib.Path(__file__).resolve().parent.parent / 'research' / 'sources'
OUT.mkdir(parents=True, exist_ok=True)
REPOS = {'ardour': ('Ardour/ardour', 'master'), 'lmms': ('LMMS/lmms', 'master'),
         'zrythm': ('zrythm/zrythm', 'master'), 'tracktion': ('Tracktion/tracktion_engine', 'develop')}

def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'Rhino-architecture-research'})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()

def snapshot(item):
    name, (repo, branch) = item
    commit = json.loads(fetch(f'https://api.github.com/repos/{repo}/commits/{branch}'))
    sha = commit['sha']
    tree = json.loads(fetch(f'https://api.github.com/repos/{repo}/git/trees/{sha}?recursive=1'))
    paths = [entry['path'] for entry in tree['tree'] if entry['type'] == 'blob']
    directory = OUT / name
    directory.mkdir(exist_ok=True)
    metadata = {'repository': repo, 'branch': branch, 'commit': sha, 'commit_date': commit['commit']['committer']['date'], 'tree_truncated': tree.get('truncated', False), 'paths': paths}
    (directory / 'index.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    return f'{name}: {sha}, {len(paths)} files'

FILES = {
 'ardour': ['libs/ardour/ardour/audioengine.h', 'libs/ardour/ardour/processor.h', 'libs/ardour/ardour/region.h', 'libs/ardour/ardour/graph.h', 'libs/ardour/session_process.cc', 'libs/ardour/disk_reader.cc', 'COPYING'],
 'lmms': ['include/AudioEngine.h', 'include/Instrument.h', 'include/Effect.h', 'include/AutomatableModel.h', 'src/core/AudioEngine.cpp', 'src/core/Song.cpp', 'README.md'],
 'zrythm': ['src/dsp/engine.h', 'src/dsp/graph_scheduler.h', 'src/dsp/graph_builder.h', 'src/dsp/processor_base.h', 'src/dsp/parameter.h', 'src/dsp/playhead_qml_adapter.h', 'src/commands/change_parameter_value_command.h', 'src/engine-process/ipc_message.h', 'src/engine-process/audio_engine_application.cpp', 'README.md'],
 'tracktion': ['modules/tracktion_engine/plugins/tracktion_Plugin.h', 'modules/tracktion_engine/model/edit/tracktion_Edit.h', 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h', 'modules/tracktion_engine/playback/graph/tracktion_ArrangerLauncherSwitchingNode.h', 'modules/tracktion_engine/playback/graph/tracktion_PluginNode.h', 'modules/tracktion_engine/model/clips/tracktion_WaveAudioClip.h', 'LICENSE.md', 'FEATURES.md']
}

def source(item):
    name, path = item
    directory = OUT / name
    metadata = json.loads((directory / 'index.json').read_text(encoding='utf-8'))
    if path not in metadata['paths']:
        return {'repository': name, 'path': path, 'error': 'Path absent in snapshot'}
    api_url = f"https://api.github.com/repos/{metadata['repository']}/contents/{path}?ref={metadata['commit']}"
    payload = json.loads(fetch(api_url))
    data = base64.b64decode(payload['content'])
    filename = path.replace('/', '__') + '.txt'
    (directory / filename).write_bytes(data)
    return {'repository': name, 'path': path, 'commit': metadata['commit'], 'url': f"https://github.com/{metadata['repository']}/blob/{metadata['commit']}/{path}", 'local': f'{name}/{filename}', 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}

if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        if '--files' in sys.argv:
            results = list(pool.map(source, [(name, path) for name, paths in FILES.items() for path in paths]))
            (OUT / 'manifest.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
            for result in results:
                print(result['repository'], result['path'], result.get('bytes', result.get('error')))
        else:
            for result in pool.map(snapshot, REPOS.items()):
                print(result)
