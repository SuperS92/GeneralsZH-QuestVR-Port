#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual optional AA compute pass on the host Vulkan GPU, isolated from game logic."""
import argparse, json, pathlib, re, shutil, subprocess, os

root = pathlib.Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args = parser.parse_args()
tools = json.loads(args.tools.read_text())
work = root / 'work/spatial-aa-smoke'
work.mkdir(exist_ok=True)
sysroot = pathlib.Path(tools['ndk']) / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('vulkan', 'vk_video'):
    shutil.copytree(sysroot / folder, work / 'include' / folder, dirs_exist_ok=True)
source = root / 'GeneralsMD/Code/Main/QuestVR/QuestSpatialAA.cpp'
test = root / 'scripts/qa/quest/test-spatial-aa.cpp'
smaa = source.with_name('QuestSMAA.cpp')
performance = source.with_name('QuestPerformance.cpp')
functions = sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(', source.read_text() + smaa.read_text() + test.read_text())))
definition = work / 'vulkan.def'
definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + '\n'.join(functions) + '\n', encoding='utf-8')
compiler = pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler / 'llvm-dlltool.exe'), '-m', 'i386:x86-64', '-d', str(definition), '-l', str(work / 'libvulkan.a')], check=True)
exe = work / 'aa-smoke.exe'
command = [str(compiler / 'clang++.exe'), '-std=c++17', '-static', '-D__ANDROID__', '-DSAGE_USE_OPENXR',
    '-I' + str(work / 'include'), '-I' + str(root / 'scripts/qa/quest/host-shims'),
    '-I' + str(source.parent), '-I' + str(root / 'Core/GameEngineDevice/Include'), str(source), str(smaa), str(performance), str(test), str(work / 'libvulkan.a'), '-o', str(exe)]
subprocess.run(command, check=True)
checksums=[]
for enabled in ('0','1'):
    env=os.environ.copy(); env['GX_QUEST_PERF_SMAA']=enabled
    result = subprocess.run([str(exe),str(work / ("output-scratch-"+enabled+".bin"))], capture_output=True, text=True, timeout=90, env=env)
    log = result.stdout + result.stderr
    (work / ('result-scratch-'+enabled+'.log')).write_text(log, encoding='utf-8')
    print('SMAA shared edge scratch:', enabled, log[-1200:])
    if result.returncode: raise SystemExit(result.returncode)
    checksums.append(re.search(r'OUTPUT_CHECKSUM=([0-9a-f]{16})',log).group(1))
assert checksums[0]==checksums[1], 'Shared scratch changed actual GPU output'
original=(work/'output-scratch-0.bin').read_bytes()
shared=(work/'output-scratch-1.bin').read_bytes()
assert original and original==shared, 'Actual readback pixels differ with shared scratch'
print('GPU output checksum exact across original/shared scratch:',checksums[0])
report = {'status': 'HOST_GPU_SHADER_PASS', 'modes': ['gentle spatial AA', 'original SMAA 1x HIGH'], 'cases': 120, 'smaaScratchComparison':'EXACT_GPU_OUTPUT', 'comparedBytes':len(original), 'outputChecksum':checksums[0],
    'checks': 'RGBA/BGRA; separate red/blue eyes; flat color exact; opposite diagonal edges blended; small texture features without halos; gentle filter retains half original contrast; axis-aligned single-pixel stripe exact; two fence slots in flight and reuse; changed source removes stale edges/weights; SMAA missing queue/unsampleable-eye fallback; non-workgroup-aligned extent',
    'questRuntime': 'NOT_VERIFIED', 'logs': ['work/spatial-aa-smoke/result-scratch-0.log','work/spatial-aa-smoke/result-scratch-1.log']}
(root / 'work/evidence/spatial-aa-host.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
