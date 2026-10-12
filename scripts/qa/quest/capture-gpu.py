#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private Quest GPU evidence. Never installs, launches, stops the game or clears logs.

Detailed tracing must be prepared while the game is closed. OVR Metrics CSV
recording is configured by the user; this tool does not overwrite that setup.
"""
import argparse,datetime,json,pathlib,re,subprocess,threading,time

PACKAGE='com.generalsx.questvr'
CSV_ROOT='/sdcard/Android/data/com.oculus.ovrmonitormetricsservice/files/CapturedMetrics'
COUNTERS=('GPU Frequency','GPU % Utilization','GPU % Bus Busy','% Shaders Busy',
    '% Shaders Stalled','% Texture Fetch Stall','% Texture L1 Miss','% Texture L2 Miss',
    '% Stalled on System Memory','Read Total (Bytes/sec)','Write Total (Bytes/sec)',
    '% Time Shading Vertices','% Time Shading Fragments','% Time Compute')

def utc(): return datetime.datetime.now(datetime.timezone.utc).isoformat()
def catalog(text):
    return {name.strip():int(number) for number,name in re.findall(r'^\s*(\d+)\s+([^\r\n]+)$',text,re.M)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb',default='adb')
    parser.add_argument('--serial',default='')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--mode',choices=('probe','prepare-trace','baseline','trace','restore'),default='probe')
    parser.add_argument('--seconds',type=int,default=180)
    parser.add_argument('--countdown',type=int,default=15)
    args=parser.parse_args()
    if not 15<=args.seconds<=600 or not 0<=args.countdown<=60: parser.error('Invalid capture duration/countdown')
    devices=subprocess.check_output([args.adb,'devices'],text=True)
    ready=re.findall(r'^([A-Za-z0-9._:-]+)\s+device\s*$',devices,re.M)
    serial=args.serial or (ready[0] if len(ready)==1 else '')
    if not serial or serial not in ready: raise SystemExit('Connect exactly one authorized Quest, or select its serial.')
    command=[args.adb,'-s',serial]
    args.output.mkdir(parents=True,exist_ok=True)
    owner=args.output/'profiling-owner.json'
    def read(*parts,required=True,timeout=25):
        result=subprocess.run(command+list(parts),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        if required and result.returncode: raise RuntimeError('ADB operation failed: '+parts[0])
        return result.stdout.strip(),result.returncode
    def detailed():
        value,_=read('shell','ovrgpuprofiler','-i')
        if 'disabled' in value.lower(): return False
        if 'enabled' in value.lower(): return True
        raise RuntimeError('Unknown detailed profiling state; do not overwrite it.')
    if args.mode=='restore':
        if owner.exists():
            state=json.loads(owner.read_text())
            if state.get('enabledByThisTool'):
                read('shell','ovrgpuprofiler','-d')
                owner.write_text(json.dumps(dict(enabledByThisTool=False,restoredUtc=utc()))+'\n')
                print('Detailed profiling disabled. Fully close and restart the game before a normal comparison.')
        else: print('No profiling state owned by this tool; nothing changed.')
        return
    metrics,_=read('shell','ovrgpuprofiler','-m')
    available=catalog(metrics)
    if not available: raise RuntimeError('No hardware metric catalog detected.')
    if args.mode=='prepare-trace':
        pid,_=read('shell','pidof',PACKAGE,required=False)
        if pid: raise RuntimeError('Fully close the game AND launcher before preparing a fresh profiling context.')
        if detailed(): raise RuntimeError('Detailed profiling is already enabled; restore the previous owner first.')
        # Record ownership before enabling so an interrupted operation can be restored.
        owner.write_text(json.dumps(dict(enabledByThisTool=True,preparedUtc=utc(),package=PACKAGE))+'\n')
        read('shell','ovrgpuprofiler','-e',PACKAGE)
        if not detailed(): raise RuntimeError('The runtime did not enable detailed profiling.')
        print('Prepared detailed GPU profiling for the Quest game only. Now start a fresh game and warm it up.')
        return
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    folder=args.output/(args.mode+'-'+stamp);folder.mkdir(exist_ok=False)
    (folder/'counter-catalog.txt').write_text(metrics+'\n',encoding='utf-8')
    help_text,_=read('shell','ovrgpuprofiler','-h')
    (folder/'profiler-help.txt').write_text(help_text+'\n',encoding='utf-8')
    package_info,_=read('shell','dumpsys','package',PACKAGE)
    versions=re.findall(r'version(?:Name|Code)=\S+',package_info)
    model,_=read('shell','getprop','ro.product.model')
    meta=dict(startUtc=utc(),mode=args.mode,model=model,versions=versions,detailedProfiling=detailed(),
        counterScope='Whole GPU, including compositor; not exclusively the game',
        caveat='Native subsystem times are CPU; copy_gpu excludes DXVK world rendering. Trace profiling has overhead.')
    selected={name:available[name] for name in COUNTERS if name in available}
    meta['selectedCounters']=selected
    meta['unsupportedCounters']=[name for name in COUNTERS if name not in available]
    if args.mode=='probe':
        meta['ovrMetricsInstalled']=bool(read('shell','pm','path','com.oculus.ovrmonitormetricsservice',required=False)[0])
        (folder/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n')
        print('Probe saved: '+str(folder));return
    if args.mode=='baseline' and meta['detailedProfiling']: raise RuntimeError('Restore detailed profiling and restart the game before a baseline.')
    if args.mode=='trace' and (not meta['detailedProfiling'] or not owner.exists() or not json.loads(owner.read_text()).get('enabledByThisTool')):
        raise RuntimeError('Prepare a dedicated trace run with this tool first.')
    pid,_=read('shell','pidof',PACKAGE,required=False)
    if not re.fullmatch(r'\d+(?: \d+)*',pid): raise RuntimeError('Open the Quest game and load a skirmish before capturing.')
    print(f'Capture starts in {args.countdown} seconds; {args.seconds} seconds. Keep the same settings; do not record video.',flush=True)
    time.sleep(args.countdown)
    meta['captureStartUtc']=utc()
    meta['deviceEpoch']=read('shell','date','+%s')[0]
    workers=[];errors=[]
    def stream(parts,path):
        try:
            with path.open('w',encoding='utf-8') as out:
                process=subprocess.Popen(command+parts,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
                try:
                    for line in process.stdout: out.write(utc()+' '+line);out.flush()
                    code=process.wait(timeout=5)
                    if code not in (0,124,143): errors.append(dict(file=path.name,exit=code))
                finally:
                    if process.poll() is None: process.terminate();process.wait(timeout=5)
        except Exception as failure: errors.append(dict(file=path.name,error=type(failure).__name__))
    ids=','.join(str(value) for value in selected.values())
    startup,_=read('logcat','-d','--pid='+pid.split()[0],'-v','epoch','GeneralsXR:I','*:S',required=False)
    (folder/'native-startup.log').write_text(startup+'\n',encoding='utf-8')
    # The profiler line-buffers its console only on a TTY. Force a remote PTY so
    # a bounded timeout does not discard buffered samples when the process exits.
    for parts,name in [(['shell','-tt','timeout',str(args.seconds),'ovrgpuprofiler','-r'+ids],'gpu-counters.log'),
        (['shell','timeout',str(args.seconds),'logcat','-T','1','--pid='+pid.split()[0],'-v','epoch','GeneralsXR:I','*:S'],'native-performance.log')]:
        worker=threading.Thread(target=stream,args=(parts,folder/name),daemon=True);worker.start();workers.append(worker)
    end=time.monotonic()+args.seconds
    trace_index=0;next_trace=time.monotonic()+10
    while time.monotonic()<end:
        if args.mode=='trace' and time.monotonic()>=next_trace:
            trace_index+=1
            value,code=read('shell','ovrgpuprofiler','-t0.25','-v',required=False,timeout=20)
            (folder/f'render-trace-{trace_index:02d}.txt').write_text(utc()+'\n'+value+'\n',encoding='utf-8')
            if code: errors.append(dict(trace=trace_index,exit=code))
            next_trace=time.monotonic()+30
        time.sleep(0.25)
    for worker in workers:
        worker.join(timeout=12)
        if worker.is_alive(): errors.append(dict(error='Capture worker did not finish'))
    # Only the current game's metrics files, never other apps' sessions.
    files,_=read('shell','ls','-1',CSV_ROOT,required=False)
    names=[name for name in files.splitlines() if PACKAGE in name and re.fullmatch(r'[A-Za-z0-9._-]+\.csv',name)]
    csv_dir=folder/'ovr-metrics';csv_dir.mkdir()
    csv_epochs={}
    for name in sorted(names):
        modified,_=read('shell','stat','-c','%Y',CSV_ROOT+'/'+name,required=False)
        if not modified.isdigit() or int(modified)<int(meta['deviceEpoch'])-5: continue
        csv_epochs[name]=int(modified)
        _,code=read('pull',CSV_ROOT+'/'+name,str(csv_dir/name),required=False,timeout=60)
        if code: errors.append(dict(csv=name,exit=code))
    meta.update(endUtc=utc(),durationSeconds=args.seconds,errors=errors,ovrCsvFiles=[path.name for path in csv_dir.glob('*.csv')],csvDeviceModifiedEpoch=csv_epochs,traceFiles=trace_index)
    meta['status']='CAPTURED_REVIEW_REQUIRED' if not errors else 'PARTIAL_CAPTURE'
    if not meta['ovrCsvFiles']: meta['missingAppGpuTime']='Enable OVR Metrics CSV and Advanced stats before a fresh game; hardware counters alone do not give app GPU frame time.'
    (folder/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
    print('Saved: '+str(folder),flush=True)
    if args.mode=='trace': print('Next: restore profiling, then fully close and restart the game. Do not compare detailed-trace FPS directly with baseline FPS.')
    if errors or not meta['ovrCsvFiles']: print('Some data needs review; see manifest.json. This is not a complete bottleneck diagnosis.')

if __name__=='__main__': main()
