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
functions=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(',source.read_text()+test.read_text()+test.with_name('taa-vulkan-contract.h').read_text())))
definition=work/'vulkan.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(functions)+'\n')
compiler=pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libvulkan.a')],check=True)
exe=work/'taa-smoke.exe'
compileCommand=[str(compiler/'clang++.exe'),'-std=c++17','-static','-D__ANDROID__','-DSAGE_USE_OPENXR',
    '-I'+str(work/'include'),'-I'+str(root/'scripts/qa/quest/host-shims'),'-I'+str(source.parent),
    '-I'+str(root/'Core/GameEngineDevice/Include'),str(source),str(test),str(work/'libvulkan.a'),'-o',str(exe)]
logs=[]
for width,height in ((17,13),(31,23)):
    subprocess.run(compileCommand+['-DQUEST_TAA_TEST_WIDTH='+str(width),'-DQUEST_TAA_TEST_HEIGHT='+str(height)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90)
    log=f'Dimensions {width}x{height}\n'+result.stdout+result.stderr
    logs.append(log);(work/'result.log').write_text('\n'.join(logs),encoding='utf-8')
    print(result.stdout)
    if result.returncode:
        print(result.stderr);raise SystemExit(result.returncode)
log='\n'.join(logs)
report={'status':'HOST_GPU_TAA_PASS','cases':sum(map(int,re.findall(r'PASS (\d+) GPU frame cases',log))),
    'qualities':['Full','Medium','Lite'],
    'checks':['All quality pipelines execute with transfers and GPU bundle Off/On','Compact history RGB within one UNORM8 step; alpha/periphery/reset exact','Known native stage/access scopes and conservative DXVK boundaries','Unchanged-layout modes preserve every GPU output byte','Unsupported source copy usage falls back to original apply','MRT blend states identical without independentBlend','Lite peripheral pixels byte-exact; bounded centered history','Independent row-vector D3D projection oracle: 1000 points','RGBA/BGRA, separate eyes',
    'D32/D24S8 direct sampled depth and private depth copy','Every pixel alpha preserved',
    'First/reset raw bytes exact','Flat replacement rejects stale history','Temporal attenuation of alternating texture',
    'Actual GPU orthographic and perspective camera reprojection with independent expected pixels',
    'Shared depth overwritten between eyes: distinct .25/.75 depth selects the correct different reprojection offsets',
    'Actual image barriers and descriptors checked against creation usage; VUID 01208 through 01213'],
    'questBridge':'NOT_VERIFIED','questPerformance':'NOT_MEASURED','validationLayers':'NOT_REQUESTED',
    'logs':['work/temporal-aa-smoke/result.log']}
(root/'work/evidence/temporal-aa-host.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
