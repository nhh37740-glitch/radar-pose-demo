"""Exercise the actual native EXE/DLLs against recorded sensor data, no mocked IPC."""
import argparse
import csv
import ctypes
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

OPTIONS = None
NAMES = ['参考位置','雷达主导估计','多帧估计','单帧估计','图像主导估计','仅图像里程计','仅雷达里程计']

class NativePlayback(unittest.TestCase):
    def setUp(self):
        self.folder=OPTIONS.evidence/self._testMethodName
        self.folder.mkdir(parents=True,exist_ok=True)
        self.children=[]
        self.logs=[]
        self.index=json.loads((OPTIONS.data/'manifest.json').read_text(encoding='utf-8'))
        self.count=self.index['metadata']['sampleCount']
        self.assertEqual(self.count,OPTIONS.expected_count)

    def launch(self,*args):
        env=dict(os.environ)
        if OPTIONS.isolated:
            env['PATH']=str(OPTIONS.bin)+os.pathsep+str(Path(env.get('SystemRoot','C:/Windows'))/'System32')
            env.pop('QTDIR',None)
            env.pop('QT_PLUGIN_PATH',None)
        env['QT_QPA_PLATFORM']='offscreen'
        env['QT_QPA_FONTDIR']=str(Path(env.get('SystemRoot','C:/Windows'))/'Fonts')
        log=open(self.folder/('process-'+str(len(self.children))+'.log'),'wb')
        child=subprocess.Popen([str(OPTIONS.bin/'radar-playback.exe'),'--headless','--data',str(OPTIONS.data),*map(str,args)],cwd=OPTIONS.bin,env=env,stdout=log,stderr=subprocess.STDOUT)
        self.children.append(child)
        self.logs.append(log)
        return child

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=5)
                except subprocess.TimeoutExpired:child.kill();child.wait(timeout=5)
        for log in self.logs:log.close()

    def read_report(self,path):
        value=json.loads(path.read_text(encoding='utf-8'))
        self.assertTrue(value['workerStopped'],value)
        self.assertTrue(all(value['threads'].values()),value)
        self.assertFalse(value['liveInference'])
        self.assertEqual(value['recordingMode'],'saved-per-frame-estimates')
        return value

    def original(self,number):
        page=self.index['pages'][number//240]
        return json.loads((OPTIONS.data/page['file']).read_text(encoding='utf-8'))['frames'][number-page['startFrame']]

    def test_seek_pages_last_frame_and_real_widget_screenshot(self):
        targets=sorted(set([0,min(239,self.count-1),min(240,self.count-1),self.count//2,self.count-1]))
        report=self.folder/'report.json';image=self.folder/'interface.png'
        child=self.launch('--probe-frames',','.join(map(str,targets)),'--report',report,'--screenshot',image)
        self.assertEqual(child.wait(timeout=60),0)
        result=self.read_report(report)
        self.assertEqual(result['shownIndices'],targets,result)
        self.assertEqual(result['errors'],[],result)
        self.assertEqual(result['currentPoseRow'],self.original(self.count-1))
        self.assertEqual(result['radarImageWidth'],480)
        self.assertEqual(result['stereoImageWidth'],480)
        self.assertGreater(image.stat().st_size,25000)

    def test_export_every_pose_and_error_matches_source(self):
        report=self.folder/'report.json';output=self.folder/'all-methods.csv'
        child=self.launch('--export',output,'--report',report)
        self.assertEqual(child.wait(timeout=90),0)
        result=self.read_report(report)
        self.assertEqual(result['exportedRows'],self.count)
        with output.open(encoding='utf-8',newline='') as stream:rows=list(csv.DictReader(stream))
        self.assertEqual(len(rows),self.count)
        self.assertEqual(len(rows[0]),46)
        for page in self.index['pages']:
            source=json.loads((OPTIONS.data/page['file']).read_text(encoding='utf-8'))['frames']
            for saved in source:
                actual=rows[saved[0]]
                self.assertEqual(int(actual['index']),saved[0])
                self.assertEqual(int(actual['radar_timestamp']),saved[1])
                self.assertEqual(int(actual['stereo_timestamp']),saved[31])
                self.assertEqual(int(actual['vo_model_accepted']),saved[30])
                for method,name in enumerate(NAMES):
                    for column,offset in [('north',0),('east',1),('down',2),('yaw',3)]:
                        self.assertEqual(float(actual[name+'_'+column]),saved[2+4*method+offset])
                    error=math.hypot(saved[2+4*method]-saved[2],saved[3+4*method]-saved[3])
                    angle=abs(math.remainder(saved[5+4*method]-saved[5],360))
                    self.assertAlmostEqual(float(actual[name+'_planar_error']),error,places=9)
                    self.assertAlmostEqual(float(actual[name+'_yaw_error']),angle,places=9)

    def test_play_to_end_does_not_keep_looping(self):
        report=self.folder/'report.json'
        child=self.launch('--start-frame',self.count-3,'--auto-play','--speed',16,'--duration-ms',1800,'--report',report)
        self.assertEqual(child.wait(timeout=60),0)
        result=self.read_report(report)
        self.assertEqual(result['shownIndices'],list(range(self.count-3,self.count)),result)
        self.assertEqual(result['errors'],[],result)

    def test_paused_program_does_not_spin_cpu(self):
        ready=self.folder/'ready.json';report=self.folder/'report.json'
        child=self.launch('--ready-file',ready,'--duration-ms',3200,'--report',report)
        deadline=time.monotonic()+30
        while not ready.exists() and time.monotonic()<deadline:
            self.assertIsNone(child.poll(),'Exited before becoming ready')
            time.sleep(.05)
        self.assertTrue(ready.exists())
        def cpu_time():
            values=[ctypes.c_ulonglong() for _ in range(4)]
            get_times=ctypes.WinDLL('kernel32',use_last_error=True).GetProcessTimes
            get_times.argtypes=[ctypes.c_void_p]+[ctypes.POINTER(ctypes.c_ulonglong)]*4
            get_times.restype=ctypes.c_int
            self.assertTrue(get_times(int(child._handle),*[ctypes.byref(v) for v in values]))
            return (values[2].value+values[3].value)/10000000
        time.sleep(.3)
        before=cpu_time();time.sleep(.8);used=cpu_time()-before
        self.assertLess(used,.12,'Paused process unexpectedly consumes CPU seconds')
        self.assertEqual(child.wait(timeout=60),0)
        result=self.read_report(report)
        self.assertEqual(result['shownIndices'],[0])
        (self.folder/'idle-cpu.json').write_text(json.dumps({'sampleSeconds':.8,'processCpuSeconds':used}),encoding='utf-8')

    def test_invalid_controls_and_missing_pack_fail_visibly(self):
        for args in [('--speed','0'),('--start-frame',str(self.count)),('--probe-frames',str(self.count)),('--data',str(self.folder/'absent'))]:
            child=self.launch(*args,'--duration-ms',1500)
            self.assertNotEqual(child.wait(timeout=60),0)

    def test_export_failure_does_not_replace_original(self):
        target=self.folder/'existing.csv';target.write_text('keep original',encoding='utf-8')
        child=self.launch('--export',target,'--export-last',self.count,'--duration-ms',1500)
        self.assertNotEqual(child.wait(timeout=60),0)
        self.assertEqual(target.read_text(encoding='utf-8'),'keep original')

    def test_decode_complete_recorded_sequence(self):
        if not OPTIONS.full_scan:self.skipTest('Full decode is a separate explicit gate')
        report=self.folder/'report.json'
        child=self.launch('--verify-all','--report',report)
        self.assertEqual(child.wait(timeout=240),0)
        result=self.read_report(report)
        self.assertEqual(result['validatedFrames'],self.count)
        self.assertEqual(result['errors'],[],result)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--bin',type=Path,required=True)
    parser.add_argument('--data',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--expected-count',type=int,required=True)
    parser.add_argument('--isolated',action='store_true')
    parser.add_argument('--full-scan',action='store_true')
    OPTIONS=parser.parse_args()
    OPTIONS.bin=OPTIONS.bin.resolve();OPTIONS.data=OPTIONS.data.resolve()
    evidence=OPTIONS.evidence.resolve();OPTIONS.evidence=evidence/('run-'+str(time.time_ns()))
    results=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(NativePlayback))
    evidence.mkdir(parents=True,exist_ok=True)
    (evidence/'summary.json').write_text(json.dumps({'passed':results.wasSuccessful(),'tests':results.testsRun,'failures':len(results.failures),'errors':len(results.errors),'skipped':len(results.skipped),'expectedFrames':OPTIONS.expected_count,'fullDecodeGate':OPTIONS.full_scan,'isolatedRuntime':OPTIONS.isolated,'runDir':str(OPTIONS.evidence)},indent=2),encoding='utf-8')
    sys.exit(0 if results.wasSuccessful() else 1)
