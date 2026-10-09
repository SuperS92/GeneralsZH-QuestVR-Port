#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise user entry and ray selection on the production Quest keyboard model."""
import argparse, json, pathlib, subprocess
root = pathlib.Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args = parser.parse_args()
tools = json.loads(args.tools.read_text())
work = root / 'work/keyboard-test'
work.mkdir(exist_ok=True)
exe = work / 'test.exe'
subprocess.run([str(pathlib.Path(tools['hostCxxRuntime']) / 'clang++.exe'), '-std=c++17', '-O2', '-static',
               '-I' + str(root / 'Core/GameEngineDevice/Include'), str(root / 'scripts/qa/quest/test-keyboard.cpp'),
               '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
(work / 'result.log').write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr)
if result.returncode: raise SystemExit(result.returncode)
(root / 'work/evidence/keyboard-host.json').write_text(json.dumps({
    'status': 'HOST_KEYBOARD_MODEL_PASS', 'productionModel': True,
    'limits': 'Engine field-focus/serial integration, headset rendering/interaction and online runtime not exercised here.'
}, indent=2))
