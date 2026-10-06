"""Extract and execute the actual delivery ZIPs using their bundled runtimes."""
import argparse, hashlib, json, os, shutil, subprocess, sys, zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--dist', type=Path, required=True)
parser.add_argument('--destination', type=Path, required=True)
args = parser.parse_args()
dist = args.dist.resolve()
destination = args.destination.resolve()
if destination.exists():
    raise SystemExit('Choose a new delivery directory; existing files are preserved.')
latest = json.loads((dist/'latest.json').read_text(encoding='utf-8'))
assert latest['bundledFrames'] == 7203
destination.mkdir(parents=True)
archives = destination/'archives'
archives.mkdir()

def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024*1024):
            result.update(chunk)
    return result.hexdigest()

def unpack(path, target):
    target.mkdir()
    with zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            assert (target/entry.filename).resolve().is_relative_to(target.resolve())
        archive.extractall(target)

full = destination/'runtime'
program = destination/'program-only'
for entry in latest['archives']:
    source = dist/entry['name']
    assert source.stat().st_size == entry['size'] and digest(source) == entry['sha256']
    copy = archives/source.name
    shutil.copyfile(source, copy)
    assert digest(copy) == entry['sha256']
    if source.name.startswith('radar-qt-full-'):
        unpack(copy, full)
    elif source.name.startswith('radar-qt-program-'):
        unpack(copy, program)
    else:
        module = source.name.removeprefix('radar_').split('-')[0]
        target = destination/('module-'+module)
        unpack(copy, target)
        for item in target.rglob('*'):
            if item.is_file():
                assert digest(item) == digest(full/'modules'/module/item.relative_to(target))
for name in ['SHA256SUMS.txt', 'latest.json']:
    shutil.copyfile(dist/name, archives/name)
subprocess.run([sys.executable, str(Path(__file__).with_name('audit_delivery.py')), '--root', str(full)], check=True)
evidence = destination/'archive-evidence'
evidence.mkdir()
data = full/'programs/radar-playback/full'

def run(binary, arguments):
    environment = dict(os.environ)
    environment['PATH'] = str(binary.parent)+os.pathsep+str(Path(environment.get('SystemRoot', 'C:/Windows'))/'System32')
    environment['QT_QPA_PLATFORM'] = 'offscreen'
    environment['QT_QPA_FONTDIR'] = str(Path(environment.get('SystemRoot', 'C:/Windows'))/'Fonts')
    for name in ['QTDIR', 'QT_PLUGIN_PATH']:
        environment.pop(name, None)
    result = subprocess.run([str(binary), *map(str, arguments)], env=environment, cwd=evidence, capture_output=True, text=True, timeout=90)
    (evidence/(binary.stem+'-'+str(len(list(evidence.glob('*.log'))))+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout+result.stderr

full_report = evidence/'full-archive.json'
run(full/'programs/radar-playback/radar-playback.exe', ['--headless', '--probe-frames', '0,239,240,7202', '--report', full_report, '--screenshot', evidence/'interface.png'])
program_report = evidence/'program-archive.json'
run(program/'radar-playback.exe', ['--headless', '--data', data, '--probe-frames', '0,239,240,7202', '--export', evidence/'last-two.csv', '--export-first', '7201', '--export-last', '7202', '--report', program_report])
for report in [full_report, program_report]:
    value = json.loads(report.read_text(encoding='utf-8'))
    assert value['frameCount'] == 7203 and value['currentIndex'] == 7202
    assert value['shownIndices'] == [0,239,240,7202]
    assert value['workerStopped'] and all(value['threads'].values()) and not value['errors']
assert json.loads(program_report.read_text(encoding='utf-8'))['exportedRows'] == 2
run(full/'shared/bin/radar-sdk-consumer.exe', [full/'shared/bin', data])
result = {'passed': True, 'archives': len(latest['archives']), 'bundledFrames': 7203, 'isolatedRuntime': True, 'sdkConsumerPassed': True, 'revision': latest['revision'], 'destination': str(destination)}
for path in [dist/'archive-verification.json', evidence/'archive-verification.json']:
    path.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
