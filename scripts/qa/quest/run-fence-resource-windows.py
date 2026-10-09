#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real fence upload/marker code against a resource interface fixture."""
import argparse,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=pathlib.Path);args=p.parse_args()
tools=json.loads(args.tools.read_text());folder=root/'work/fence-resource-smoke';folder.mkdir(exist_ok=True)
exe=folder/'fixture.exe'
subprocess.run([str(pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'),'-std=c++17','-static',
 '-D__ANDROID__','-DSAGE_USE_OPENXR','-DQUEST_FENCE_CONTOUR_NATIVE_FIXTURE',
 '-I'+str(root/'Core/GameEngineDevice/Include'),'-I'+str(root/'scripts/qa/quest/host-shims'),
 str(root/'GeneralsMD/Code/Main/QuestVR/QuestFenceContours.cpp'),str(pathlib.Path(__file__).with_name('test-fence-resource.cpp')),
 '-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
(folder/'result.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr)
result.check_returncode()
record={'status':'HOST_RESOURCE_FIXTURE_PASS','tested':'production upload and private-marker code; Off, failures, unlock ordering, reload, counters',
 'nativeGPU':'NOT_VERIFIED','crossplay':'NOT_VERIFIED'}
(root/'work/evidence/fence-resource-host.json').write_text(json.dumps(record,indent=2)+'\n')
