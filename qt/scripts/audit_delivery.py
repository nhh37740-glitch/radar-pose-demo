"""Verify actual x64 binaries, SDK imports, scope, tests and every file hash."""
import argparse,hashlib,json,struct
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True);args=parser.parse_args()
root=args.root.resolve();manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
modules={'contracts','data_reader','playback','frontend'}
assert set(manifest['modules'])==modules
def pe(path,dll):
    data=path.read_bytes();assert len(data)>4096 and data[:2]==b'MZ',path
    offset=struct.unpack_from('<I',data,0x3c)[0];assert data[offset:offset+4]==b'PE\0\0',path
    assert struct.unpack_from('<H',data,offset+4)[0]==0x8664,path
    assert bool(struct.unpack_from('<H',data,offset+22)[0]&0x2000)==dll,path
program=root/'programs/radar-playback';pe(program/'radar-playback.exe',False)
for runtime in [program,root/'shared/bin']:
    for name in ['Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','vcruntime140.dll','msvcp140.dll',*[f'radar_{m}.dll' for m in modules]]:pe(runtime/name,True)
    for plugin in ['platforms/qwindows.dll','platforms/qoffscreen.dll','imageformats/qjpeg.dll']:pe(runtime/plugin,True)
for module in modules:
    folder=root/'modules'/module;info=json.loads((folder/'module.json').read_text(encoding='utf-8-sig'))
    assert info['module']==module
    pe(folder/info['binary'],True)
    assert (folder/info['library']).read_bytes().startswith(b'!<arch>\n')
    assert (folder/info['header']).is_file()
    for dependency in info['dependencies']:assert (root/'shared/bin'/('radar_'+dependency+'.dll')).is_file()
for item in manifest['files']:
    path=(root/item['path']).resolve();assert path.is_relative_to(root)
    assert path.stat().st_size==item['size'],item['path']
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(1024*1024):digest.update(chunk)
    assert digest.hexdigest()==item['sha256'],item['path']
evidence=json.loads((root/'test-evidence/binary-only-summary.json').read_text(encoding='utf-8'))
assert evidence['passed'] and evidence['fullDecodeGate'] and evidence['isolatedRuntime'] and evidence['skipped']==0
assert json.loads((root/'test-evidence/sdk-consumer.json').read_text(encoding='utf-8-sig'))['modulesLoaded']==3
assert (root/'data-license.html').is_file()
for name in ['LGPL-3.0-only.txt','GPL-3.0-only.txt','Qt-GPL-exception-1.0.txt']:assert (root/'licenses'/name).stat().st_size>500
if manifest['bundledFrames']:
    assert manifest['bundledFrames']==7203
    index=json.loads((program/'full/manifest.json').read_text(encoding='utf-8'))
    assert index['metadata']['sampleCount']==7203 and index['metadata']['liveInference'] is False
    total=0;radar=set();stereo=set()
    for page in index['pages']:
        rows=json.loads((program/'full'/page['file']).read_text(encoding='utf-8'))['frames']
        for row in rows:
            assert row[0]==total;total+=1
            radar.add(str(row[1])+'.jpg');stereo.add(str(row[31])+'.jpg')
    assert total==7203
    assert {p.name for p in (program/'full/radar').iterdir()}==radar
    assert {p.name for p in (program/'full/stereo').iterdir()}==stereo
    assert evidence['expectedFrames']==7203
print(f'PASS: native EXE, 4 DLL modules, SDK import libraries, isolated runtime tests, {manifest["bundledFrames"]} bundled frames, {len(manifest["files"])} SHA256 checks.')
