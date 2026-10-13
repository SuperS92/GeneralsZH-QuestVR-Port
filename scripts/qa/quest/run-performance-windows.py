#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual Quest performance helpers and exercise output/cache gates on host."""
import argparse, json, pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
args=parser.parse_args(); tools=json.loads(args.tools.read_text())
work=root/'work/performance-smoke'; work.mkdir(exist_ok=True)
compiler=pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'
exe=work/'performance-smoke.exe'
subprocess.run([str(compiler),'-std=c++17','-static','-D__ANDROID__','-DSAGE_USE_OPENXR',
    '-I'+str(root/'Core/GameEngineDevice/Include'),'-I'+str(root/'scripts/qa/quest/host-shims'),
    str(root/'GeneralsMD/Code/Main/QuestVR/QuestPerformance.cpp'),str(root/'GeneralsMD/Code/Main/QuestVR/QuestShroudPass.cpp'),str(root/'scripts/qa/quest/test-performance.cpp'),
    '-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
log=result.stdout+result.stderr; print(log)
(work/'result.log').write_text(log)
if result.returncode: raise SystemExit(result.returncode)
report={'status':'HOST_DATA_CACHE_PASS','pcmFormats':18,'actualHelperCode':True,
    'checks':'default/exact switch; planar/interleaved output bytes; growth count; eye/frame/owner/resource/payload changes; map/input changes; shadow append/reset',
    'decoderOpenALDriverAndNativeTreeLocks':'NOT_EXERCISED','questFPSAndCrossplay':'NOT_VERIFIED'}
(root/'work/evidence/performance-host.json').write_text(json.dumps(report,indent=2)+'\n')
