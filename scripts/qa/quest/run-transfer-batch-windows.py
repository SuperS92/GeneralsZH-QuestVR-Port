#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare grouped eye/HUD transfers against sequential GPU and independent CPU pixels."""
import argparse, json, pathlib, re, shutil, subprocess
root = pathlib.Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args = parser.parse_args()
tools = json.loads(args.tools.read_text())
work = root / 'work/transfer-batch-smoke'
work.mkdir(parents=True, exist_ok=True)
sysroot = pathlib.Path(tools['ndk']) / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('vulkan', 'vk_video'):
    shutil.copytree(sysroot / folder, work / 'include' / folder, dirs_exist_ok=True)
source = root / 'GeneralsMD/Code/Main/QuestVR/QuestHudTransfer.cpp'
color = root / 'GeneralsMD/Code/Main/QuestVR/QuestColorCopy.cpp'
batch = root / 'GeneralsMD/Code/Main/QuestVR/QuestTransferBatch.cpp'
test = root / 'scripts/qa/quest/test-transfer-batch.cpp'
functions = sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(', source.read_text() + color.read_text() + batch.read_text() + test.read_text())))
definition = work / 'vulkan.def'
definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + '\n'.join(functions) + '\n', encoding='utf-8')
compiler = pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler / 'llvm-dlltool.exe'), '-m', 'i386:x86-64', '-d', str(definition), '-l', str(work / 'libvulkan.a')], check=True)
exe = work / 'transfer-batch-smoke.exe'
subprocess.run([str(compiler / 'clang++.exe'), '-std=c++17', '-static', '-D__ANDROID__', '-DSAGE_USE_OPENXR',
    '-I' + str(work / 'include'), '-I' + str(root / 'scripts/qa/quest/host-shims'), '-I' + str(source.parent), '-I' + str(root / 'Core/GameEngineDevice/Include'),
    str(source), str(color), str(batch), str(test), str(work / 'libvulkan.a'), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
log = result.stdout + result.stderr
(work / 'result.log').write_text(log, encoding='utf-8')
print(log)
if result.returncode: raise SystemExit(result.returncode)
(root / 'work/evidence/transfer-batch-host.json').write_text(json.dumps({
    'status':'HOST_GPU_PIXEL_PASS', 'gpuCases':32, 'slotsInFlight':2,
    'checks':'Actual grouped helper versus sequential GPU and CPU pixels; distinct eye sources and both destination layers; original BGRA blit and RGBA copy; full/compact/no HUD; alpha and guard; changing data and two independently submitted in-flight slots; 36864 viewport bounds oracles; alias/invalid layout/crop fallback',
    'questPerformanceAndCrossplay':'NOT_VERIFIED'}, indent=2) + '\n', encoding='utf-8')
