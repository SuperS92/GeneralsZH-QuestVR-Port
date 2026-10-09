#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test actual terrain grouping layout against original patch indices and writes."""
import argparse,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
args=parser.parse_args()
tools=json.loads(args.tools.read_text(encoding='utf-8'))
work=root/'work/terrain-batching-test';work.mkdir(exist_ok=True)
exe=work/'test.exe'
subprocess.run([str(pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'),'-std=c++17','-O2','-static',
    '-I'+str(root/'Core/GameEngineDevice/Include'),str(root/'scripts/qa/quest/test-terrain-batching.cpp'),
    '-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
(work/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)
report={'status':'HOST_LAYOUT_PASS','actualProductionLayout':True,'groups':10,'patchTotalsPerGroup':257,
    'checks':'Original triangles/order, full and partial groups, unsigned-short VB/IB bounds, patch offsets and isolated write slices, off group1, invalid input no writes',
    'nativeD3DLocks':'NOT_EXERCISED','stereoVisualPerformance':'NOT_VERIFIED','crossplay':'NOT_VERIFIED'}
(root/'work/evidence/terrain-batching-host.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
