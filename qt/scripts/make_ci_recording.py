"""Build a clearly labeled CI fixture from the existing real 240-frame excerpt.
Full production delivery still uses all 7,203 frames from prepare_full_data.py.
"""
import argparse,json,shutil
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
repo=Path(__file__).resolve().parents[2];web=repo/'web'
script=(web/'data/pose-excerpt.js').read_text(encoding='utf-8').strip()
data=json.loads(script.removeprefix('window.RADAR_POSE_METHOD_DATA=').rstrip(';'))
rows=data['frames'];assert len(rows)==240
output=args.output.resolve();assert not output.exists();output.mkdir(parents=True)
(output/'chunks').mkdir()
(output/'chunks/page-00000.json').write_text(json.dumps({'startFrame':0,'frames':rows}),encoding='utf-8')
for field,label in [(1,'radar'),(31,'stereo')]:
    (output/label).mkdir()
    for row in rows:shutil.copyfile(web/'assets'/label/(str(row[field])+'.jpg'),output/label/(str(row[field])+'.jpg'))
metadata=dict(data['metadata']);metadata.update(sampleCount=240,firstRadarTimestamp=rows[0][1],lastRadarTimestamp=rows[-1][1],recordingMode='saved-per-frame-estimates',liveInference=False,dataLicense='CC BY-NC-SA 4.0',ciFixture=True)
north=[r[2] for r in rows];east=[r[3] for r in rows]
manifest={'formatVersion':1,'pageSize':240,'metadata':metadata,'pages':[{'startFrame':0,'count':240,'file':'chunks/page-00000.json'}],'globalBounds':{'minNorth':min(north)-10,'maxNorth':max(north)+10,'minEast':min(east)-10,'maxEast':max(east)+10},'overviewRows':rows}
(output/'manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
print('CI fixture: 240 existing real frames, not the full production data pack.')
