#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real temporal Vulkan passes and an independent D3D projection oracle."""
import argparse, json, pathlib, re, shutil, subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
args=parser.parse_args();tools=json.loads(args.tools.read_text())
work=root/'work/temporal-aa-smoke';work.mkdir(exist_ok=True)
sysroot=pathlib.Path(tools['ndk'])/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('vulkan','vk_video'):
    shutil.copytree(sysroot/folder,work/'include'/folder,dirs_exist_ok=True)
source=root/'GeneralsMD/Code/Main/QuestVR/QuestTemporalAA.cpp'
test=root/'scripts/qa/quest/test-temporal-aa.cpp'
functions=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(',source.read_text()+test.read_text())))
definition=work/'vulkan.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(functions)+'\n')
compiler=pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libvulkan.a')],check=True)
exe=work/'taa-smoke.exe'
subprocess.run([str(compiler/'clang++.exe'),'-std=c++17','-static','-D__ANDROID__','-DSAGE_USE_OPENXR',
    '-I'+str(work/'include'),'-I'+str(root/'scripts/qa/quest/host-shims'),'-I'+str(source.parent),
    '-I'+str(root/'Core/GameEngineDevice/Include'),str(source),str(test),str(work/'libvulkan.a'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90)
log=result.stdout+result.stderr;(work/'result.log').write_text(log,encoding='utf-8');print(log)
if result.returncode:raise SystemExit(result.returncode)
report={'status':'HOST_GPU_TAA_PASS','cases':int(re.search(r'PASS (\d+) GPU frame cases',log)[1]),
    'checks':['Independent row-vector D3D projection oracle: 1000 points','RGBA/BGRA, separate eyes',
    'D32/D24S8 direct sampled depth and private depth copy','Every pixel alpha preserved',
    'First/reset raw bytes exact','Flat replacement rejects stale history','Temporal attenuation of alternating texture'],
    'questBridge':'NOT_VERIFIED','questPerformance':'NOT_MEASURED','validationLayers':'NOT_REQUESTED',
    'logs':['work/temporal-aa-smoke/result.log']}
(root/'work/evidence/temporal-aa-host.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
