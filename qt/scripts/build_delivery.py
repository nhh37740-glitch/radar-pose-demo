"""Stream manifests and ZIPs; preserve the complete recording without Git assets."""
import argparse,hashlib,json,subprocess,sys,zipfile
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--version',required=True);parser.add_argument('--revision',required=True);parser.add_argument('--bundled-frames',type=int,required=True);args=parser.parse_args()
root=args.root.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
files=[]
for path in sorted(p for p in root.rglob('*') if p.is_file() and p != root/'manifest.json'):
    digest=hashlib.sha256();size=0
    with path.open('rb') as source:
        while chunk:=source.read(1024*1024):digest.update(chunk);size+=len(chunk)
    files.append({'path':path.relative_to(root).as_posix(),'size':size,'sha256':digest.hexdigest()})
manifest={'version':args.version,'revision':args.revision,'platform':'windows-x64','qt':'6.8.3','modules':['contracts','data_reader','playback','frontend'],'programs':['radar-playback'],'bundledFrames':args.bundled_frames,'liveInference':False,'files':files}
(root/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
subprocess.run([sys.executable,str(Path(__file__).with_name('audit_delivery.py')),'--root',str(root)],check=True)
archives=[]
def archive(name,entries):
    target=output/name
    with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as sink:
        for relative,path in entries:
            # JPEGs are already compressed; avoid wasting CPU recompressing all14,406.
            info=zipfile.ZipInfo(relative,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_STORED if path.suffix.lower() in ['.jpg','.jpeg'] else zipfile.ZIP_DEFLATED
            with path.open('rb') as source,sink.open(info,'w',force_zip64=True) as dest:
                while chunk:=source.read(1024*1024):dest.write(chunk)
    digest=hashlib.sha256()
    with target.open('rb') as source:
        while chunk:=source.read(1024*1024):digest.update(chunk)
    archives.append({'name':name,'size':target.stat().st_size,'sha256':digest.hexdigest()})
    print(f'Created {name}: {target.stat().st_size} bytes',flush=True)
all_files=[(p.relative_to(root).as_posix(),p) for p in sorted(root.rglob('*')) if p.is_file()]
if args.bundled_frames:archive(f'radar-qt-full-{args.version}-windows-x64.zip',all_files)
else:archive(f'radar-qt-sdk-{args.version}-windows-x64.zip',all_files)
program=root/'programs/radar-playback'
program_files=[(p.relative_to(program).as_posix(),p) for p in sorted(program.rglob('*')) if p.is_file() and 'full' not in p.relative_to(program).parts]
program_files +=[(p.relative_to(root).as_posix(),p) for p in sorted((root/'licenses').rglob('*')) if p.is_file()]
program_files +=[('README.txt',root/'PROGRAM-README.txt'),('data-license.html',root/'data-license.html')]
archive(f'radar-qt-program-{args.version}-windows-x64.zip',program_files)
for module in manifest['modules']:
    folder=root/'modules'/module
    archive(f'radar_{module}-{args.version}-windows-x64.zip',[(p.relative_to(folder).as_posix(),p) for p in sorted(folder.rglob('*')) if p.is_file()])
(output/'SHA256SUMS.txt').write_text(''.join(item['sha256']+'  '+item['name']+'\n' for item in archives),encoding='utf-8')
(output/'latest.json').write_text(json.dumps({'packageRoot':str(root),'revision':args.revision,'bundledFrames':args.bundled_frames,'archives':archives},indent=2),encoding='utf-8')
print('All native delivery archives created and audited.',flush=True)
