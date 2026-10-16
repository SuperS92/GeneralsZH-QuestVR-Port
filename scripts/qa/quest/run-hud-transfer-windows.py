#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare compact HUD transfer pixels and coordinate plans against full-canvas output."""
import argparse, json, pathlib, re, shutil, subprocess
root = pathlib.Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args = parser.parse_args()
tools = json.loads(args.tools.read_text())
work = root / 'work/hud-transfer-smoke'
work.mkdir(parents=True, exist_ok=True)
sysroot = pathlib.Path(tools['ndk']) / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('vulkan', 'vk_video'):
    shutil.copytree(sysroot / folder, work / 'include' / folder, dirs_exist_ok=True)
source = root / 'GeneralsMD/Code/Main/QuestVR/QuestHudTransfer.cpp'
test = root / 'scripts/qa/quest/test-hud-transfer.cpp'
functions = sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(', source.read_text() + test.read_text())))
definition = work / 'vulkan.def'
definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + '\n'.join(functions) + '\n', encoding='utf-8')
compiler = pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler / 'llvm-dlltool.exe'), '-m', 'i386:x86-64', '-d', str(definition), '-l', str(work / 'libvulkan.a')], check=True)
exe = work / 'hud-transfer-smoke.exe'
subprocess.run([str(compiler / 'clang++.exe'), '-std=c++17', '-static', '-D__ANDROID__', '-DSAGE_USE_OPENXR',
    '-I' + str(work / 'include'), '-I' + str(root / 'scripts/qa/quest/host-shims'), '-I' + str(source.parent), '-I' + str(root / 'Core/GameEngineDevice/Include'),
    str(source), str(test), str(work / 'libvulkan.a'), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
log = result.stdout + result.stderr
(work / 'result.log').write_text(log, encoding='utf-8')
print(log)
if result.returncode: raise SystemExit(result.returncode)
(root / 'work/evidence/hud-transfer-host.json').write_text(json.dumps({
    'status':'HOST_GPU_PIXEL_PASS', 'gpuCases':64, 'slotsInFlight':2,
    'checks':'Exhaustive rectangle oracle; exact full-canvas GPU/CPU pixels; BGRA/RGBA blit and same-format copy; alpha, orientation, guard; changing crop/content and two in-flight slots; expanded UI, front-end and missing-resource fallback',
    'questPerformanceAndCrossplay':'NOT_VERIFIED'}, indent=2) + '\n', encoding='utf-8')
